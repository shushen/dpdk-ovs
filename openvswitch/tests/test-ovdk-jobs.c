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

#include <config.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

#include <rte_config.h>
#include <rte_lcore.h>

#include "ovdk_jobs.h"
#include "command-line.h"
#include "datapath/dpdk/ovdk_config.h"
#include "vlog.h"

static void test_jobs_init(int argc, char *argv[]);
static void test_jobs_add_to_lcore(int argc, char *argv[]);
static void test_jobs_clear_lcore(int argc, char *argv[]);
static void _test_jobs_inc4096(void *argp);
static void test_jobs_launch_master(int argc, char *argv[]);
static void test_jobs_launch_slave(int argc, char *argv[]);

static void
test_jobs_init(int argc, char *argv[])
{
	unsigned int i;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());

	ovdk_jobs_init();
	RTE_LCORE_FOREACH(i) {
		assert(ovdk_joblist_refs[i] != NULL);
		assert(ovdk_joblist_refs[i]->nb_jobs == 0);
		assert(ovdk_joblist_refs[i]->online == 0);
	}
	assert(MAXJOBS_PER_LCORE > 0);
}

static void
test_jobs_add_to_lcore(int argc, char *argv[])
{
	unsigned int i, j;
	int ret;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());

	ovdk_jobs_init();
	RTE_LCORE_FOREACH(i) {
		assert(ovdk_joblist_refs[i]->nb_jobs == 0);

		/* check if joblist can be filled up completely
		 * (function pointer is just random here but not NULL) */
		for (j = 0; j < MAXJOBS_PER_LCORE; ++j) {
			ret = ovdk_jobs_add_to_lcore((lcore_job_t *)(uintptr_t)(j + i + 57),
			                             (void *)(uintptr_t)(i * j),
			                             i);
			assert(ret >= 0);
			assert(ovdk_joblist_refs[i]->nb_jobs == (j + 1));
		}

		/* check if joblist is full now */
		ret = ovdk_jobs_add_to_lcore((lcore_job_t *)(uintptr_t) 437, NULL, i);
		assert(ret < 0);
	}

	RTE_LCORE_FOREACH(i) {
		/* check if list fillup happened correctly */
		assert(ovdk_joblist_refs[i]->nb_jobs == MAXJOBS_PER_LCORE);
		for (j = 0; j < MAXJOBS_PER_LCORE; ++j) {
			assert(ovdk_joblist_refs[i]->jobs[j].func == (lcore_job_t *)(uintptr_t)(j + i) + 57);
			assert(ovdk_joblist_refs[i]->jobs[j].arg  == (void *)(uintptr_t)(j * i));
		}
	}
}

static void
test_jobs_clear_lcore(int argc, char *argv[])
{
	unsigned int i, j;
	int ret;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());

	ovdk_jobs_init();
	RTE_LCORE_FOREACH(i) {
		/* add some jobs so that we have something to clear */
		ovdk_jobs_add_to_lcore((lcore_job_t *) 715, NULL, i);
		ovdk_jobs_add_to_lcore((lcore_job_t *) 386, NULL, i);
		ovdk_jobs_add_to_lcore((lcore_job_t *) 942, NULL, i);
	}

	RTE_LCORE_FOREACH(i) {
		assert(ovdk_joblist_refs[i]->nb_jobs != 0);
		ovdk_jobs_clear_lcore(i);
		assert(ovdk_joblist_refs[i]->nb_jobs == 0);
	}
}

/* Test Job: Increments a test variable. In order to avoid arithmetic
 *           overflows, this test job increments a test variable until
 *           4096 is reached */
static void
_test_jobs_inc4096(void *argp)
{
	unsigned int *testvar = argp;

	if (*testvar < 4096)
		++*testvar;
}

static void
test_jobs_launch_master(int argc, char *argv[])
{
	volatile unsigned int testvar0 = 0;
	volatile unsigned int testvar1 = 0;
	unsigned int i;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());
	/* this test has to add three jobs */
	assert(MAXJOBS_PER_LCORE >= 3);

	ovdk_jobs_init();
	ovdk_jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar0, rte_lcore_id());
	ovdk_jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar0, rte_lcore_id());
	ovdk_jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar1, rte_lcore_id());

	for (i = 0; i < 657; ++i)
		ovdk_jobs_run_master_lcore();

	assert(testvar0 == 438);
	assert(testvar1 == 219);
}

static void
test_jobs_launch_slave(int argc, char *argv[])
{
	volatile unsigned int testvar0 = 0;
	volatile unsigned int testvar1 = 0;
	enum rte_lcore_state_t slave_state;
	unsigned int slave_id;
	int ret;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());
	/* this test has to add two jobs */
	assert(MAXJOBS_PER_LCORE >= 2);
	/* we need at least one slave for this test */
	slave_id = rte_get_next_lcore(rte_lcore_id(), true, true);
	assert(slave_id < RTE_MAX_LCORE);

	ovdk_jobs_init();
	ovdk_jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar0, slave_id);
	ovdk_jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar1, slave_id);

	ret = ovdk_jobs_launch_slave_lcore(slave_id);
	assert(ret >= 0);
	slave_state = rte_eal_get_lcore_state(slave_id);
	assert(slave_state == RUNNING);

	/* wait for a while */
	sleep(5);

	ret = ovdk_jobs_stop_slave_lcore(slave_id);
	assert(ret == 0);
	slave_state = rte_eal_get_lcore_state(slave_id);
	assert(slave_state != RUNNING);

	assert(testvar0 != 0);
	assert(testvar1 != 0);
}

static const struct command commands[] = {
	{"ovdk_jobs_init", 0, 0, test_jobs_init},
	{"ovdk_jobs_add_to_lcore", 0, 0, test_jobs_add_to_lcore},
	{"ovdk_jobs_clear_lcore", 0, 0, test_jobs_clear_lcore},
	{"ovdk_jobs_launch_master", 0, 0, test_jobs_launch_master},
	{"ovdk_jobs_launch_slave", 0, 0, test_jobs_launch_slave},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	/* init EAL, parsing EAL args */
	int count = 0;

	set_program_name(argv[0]);

	count = rte_eal_init(argc, argv);
	assert(count >= 0);

	/* skip the `--` separating EAL params from test params */
	count++;

	vlog_set_levels(NULL, VLF_ANY_FACILITY, VLL_EMER);
	vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

	run_command(argc - count, argv + count, commands);

	return 0;
}
