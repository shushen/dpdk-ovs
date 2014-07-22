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

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

/* This defines how often we run a pipeline relative to how often we handle
 * messages from the vswitchd. There is a trade off here in terms of
 * responsiveness to control messages and throughput/latency of the dataplane.
 */
#define MAX_PIPELINE_RUNS_PER_CONTROL_RUN 10
static int ovdk_lcore_main_loop(__attribute__((unused)) void *arg);

static void interrupt_func(int sig);

int
main(int argc, char **argv)
{
	struct sigaction new_action;
	struct sigaction old_action;
	unsigned lcore = 0;
	int ret = 0;

	new_action.sa_handler = interrupt_func;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	sigaction(SIGINT, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction (SIGINT, &new_action, NULL);

	/* Enable correct usage message */
	rte_set_application_usage_hook(ovdk_args_usage);

	/* Init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		return -1;

	/* Disable logging */
	/* TODO - this should be done dynamically, i.e. via parameters */
	rte_set_log_level(RTE_LOG_INFO);

	/*Parse app args */
	argc -= ret;
	argv += ret;

	ovdk_args_parse_app_args(argc, argv);

	/* Carry out any initialization that needs to be done for all cores */
	ovdk_init();

	RTE_LOG(INFO, APP, "CPU frequency is %"PRIu64" MHz\n",
						rte_get_tsc_hz() / 1000000);

	/* Launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(ovdk_lcore_main_loop, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore) {
		if (rte_eal_wait_lcore(lcore) < 0) {
			return -1;
		}
	}

	return 0;
}

/*
 * This function is called on each lcore.
 */
static int
ovdk_lcore_main_loop(__attribute__((unused)) void *arg)
{
	uint32_t count = MAX_PIPELINE_RUNS_PER_CONTROL_RUN;
	unsigned lcore_id = rte_lcore_id();
	static uint64_t next_tsc = 0;
	static uint64_t curr_tsc = 0;
	uint64_t curr_tsc_local = 0;
	static uint64_t last_stats_display_tsc = 0;
	static uint64_t tsc_update_period = 0;
	uint32_t stats_interval = ovdk_args_get_stats_interval();
	unsigned vswitchd_core = ovdk_args_get_stats_core();

	RTE_LOG(INFO, APP, "Running datapath core on lcore %d\n",
			lcore_id);

	/* Carry out any per-core initialization */
	ovdk_pipeline_init();
	ovdk_datapath_init();

	RTE_LOG(INFO, APP, "Pipeline %d initialised\n", lcore_id);

	while (1) {
		/*
		 * On every core, we loop indefinitely handling any
		 * control messages from the vswitchd, running
		 * the pipeline and checking for Vhost device removal flags.
		 *
		 */
		while (count--)
			ovdk_pipeline_run();

		ovdk_datapath_handle_vswitchd_cmd();

		ovdk_vport_vhost_removal_ack(lcore_id);

		/* Display stats */
		if (lcore_id == vswitchd_core) {
			curr_tsc_local = rte_rdtsc();

			if(curr_tsc_local >= next_tsc) {
				curr_tsc = curr_tsc_local;
				next_tsc = curr_tsc_local + tsc_update_period;
			}
			if ((curr_tsc -last_stats_display_tsc) / rte_get_tsc_hz() >= stats_interval
			    && stats_interval != 0) {
				ovdk_stats_display();
				last_stats_display_tsc = curr_tsc;
			}
		}

		count = MAX_PIPELINE_RUNS_PER_CONTROL_RUN;
	}

	return 0;
}

static void
interrupt_func(int sig)
{
	ovdk_vport_vhost_teardown_cuse();
	ovdk_vport_vhost_pthread_kill();
	_exit(sig);
}
