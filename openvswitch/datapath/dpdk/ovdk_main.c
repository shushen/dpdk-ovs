/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <unistd.h> /*TODO sleep() */
#include <stdbool.h>
#include <signal.h>

#include <rte_config.h>
#include <rte_cycles.h>

#include "rte_port_vhost.h"
#include "ovdk_init.h"
#include "ovdk_mempools.h"
#include "ovdk_stats.h"
#include "ovdk_args.h"
#include "ovdk_datapath.h"
#include "ovdk_datapath_messages.h"
#include "ovdk_pipeline.h"
#include "ovdk_vport_vhost.h"
#include "ovdk_flow.h"
#include "ovdk_jobs.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

/* This defines how often we run a pipeline relative to how often we handle
 * messages from the vswitchd. There is a trade off here in terms of
 * responsiveness to control messages and throughput/latency of the dataplane.
 */
#define MAX_PIPELINE_RUNS_PER_CONTROL_RUN 10

static void do_display_stats(__rte_unused void *arg);
static void do_process_packets(__rte_unused void *arg);
static void configure_lcore(unsigned lcore_id);
static inline void __attribute__((always_inline)) configure_lcores_all(void);
static inline void __attribute__((always_inline)) master_lcore_loop(void);
static void configure_signal_handlers(void);
static void handle_signal(int sig);
int initialize_lcore(void *);

int
main(int argc, char **argv)
{
	int ret = 0;

	configure_signal_handlers();

	/* Enable correct usage message */
	rte_set_application_usage_hook(ovdk_args_usage);

	/* Init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		return -1;

	/*Parse app args */
	argc -= ret;
	argv += ret;

	ovdk_args_parse_app_args(argc, argv);

	/* Carry out any initialization that needs to be done for all cores */
	ovdk_init();

	RTE_LOG(INFO, APP, "CPU frequency is %"PRIu64" MHz\n",
						rte_get_tsc_hz() / 1000000);

	configure_lcores_all();
	ovdk_jobs_launch_slaves_all();
	master_lcore_loop();
	ovdk_jobs_stop_slaves_all();

	return 0;
}

/*
 * Stats display job for an lcore.
 */
static void
do_display_stats(__rte_unused void *arg)
{
	static uint64_t next_tsc = 0;
	static uint64_t curr_tsc = 0;
	uint64_t curr_tsc_local = 0;
	static uint64_t last_stats_display_tsc = 0;
	static uint64_t tsc_update_period = 0;
	uint32_t stats_interval = ovdk_args_get_stats_interval();

	/* Display stats */
	curr_tsc_local = rte_rdtsc();

	if(curr_tsc_local >= next_tsc) {
		curr_tsc = curr_tsc_local;
		next_tsc = curr_tsc_local + tsc_update_period;
	}
	if ((curr_tsc - last_stats_display_tsc) / rte_get_tsc_hz() >= stats_interval
	    && stats_interval != 0) {
		ovdk_stats_display();
		last_stats_display_tsc = curr_tsc;
	}
}

/*
 * Packet processing job for an lcore.
 */
static void
do_process_packets(__rte_unused void *arg)
{
	uint32_t count = MAX_PIPELINE_RUNS_PER_CONTROL_RUN;
	unsigned lcore_id = rte_lcore_id();

	/*
	 * On each core, we handle any control messages from the vswitchd, run
	 * the pipeline and check for Vhost device removal flags.
	 */
	while (count--)
		ovdk_pipeline_run();

	ovdk_datapath_handle_vswitchd_cmd();

	ovdk_vport_vhost_removal_ack(lcore_id);
}

/*
 * Assigns jobs to an lcore.
 */
static void
configure_lcore(unsigned lcore_id)
{
	int ret = 0;

	/* clear all job tables */
	ovdk_jobs_clear_lcore(lcore_id);

	/* start packet processing cores */
	ret = ovdk_jobs_add_to_lcore(do_process_packets, NULL,
				lcore_id);
	if (ret < 0)
		rte_panic("Could not setup core %d for packet "
			  "processing\n", lcore_id);

	if (ovdk_args_get_stats_core() == lcore_id) {
		ret = ovdk_jobs_add_to_lcore(do_display_stats, NULL,
		                             lcore_id);
		if (ret < 0)
			rte_panic("Could not display stats for core %d\n",
		                  lcore_id);
	}
}

/*
 * Assigns jobs to all lcores.
 */
static inline void __attribute__((always_inline))
configure_lcores_all(void)
{
	unsigned lcore_id = 0;
	int error = 0;

	error = rte_eal_mp_remote_launch(initialize_lcore, NULL, CALL_MASTER);
	if (error)
		rte_panic("Unable to initialize lcores\n");

	rte_eal_mp_wait_lcore();

	RTE_LOG(INFO, APP, "Successfully initialized lcores\n");

	RTE_LCORE_FOREACH(lcore_id) {
		configure_lcore(lcore_id);
	}
}

/*
 * Main loop.
 */
static inline void __attribute__((always_inline))
master_lcore_loop(void)
{
	for (;;)
		ovdk_jobs_run_master_lcore();

	/* TODO: Check for switch exit */
}

/*
 * Initialise a datapath instance on an lcore.
 */
int
initialize_lcore( __rte_unused void *arg)
{
	ovdk_datapath_init();
	ovdk_pipeline_init();

	return 0;
}

/*
 * Configure signal handlers.
 *
 * Configure signal handlers for application.
 */
static void
configure_signal_handlers(void)
{
	struct sigaction new_action;
	struct sigaction old_action;

	/* create a new action handler */
	new_action.sa_handler = handle_signal;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	/* but don't use if someone previously asked for signal to be ignored */
	sigaction(SIGINT, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction(SIGINT, &new_action, NULL);
	sigaction(SIGHUP, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction(SIGHUP, &new_action, NULL);
	sigaction(SIGTERM, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction(SIGTERM, &new_action, NULL);
}

/*
 * Perform cleanup on master core.
 */
static void
handle_signal(int sig)
{
	RTE_LOG(INFO, APP, "Shutting down application...\n");
	ovdk_vport_vhost_pthread_kill();
	_exit(sig);
}
