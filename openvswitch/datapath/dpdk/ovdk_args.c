/*
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>

#include <rte_memory.h>
#include <rte_string_fns.h>

#include "ovdk_args.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define PARAM_STATS "stats"
#define PARAM_STATS_CORE "stats_core"

#define PORTMASK_BASE 16

static const char *progname;
static int stats_interval = 0;
static uint64_t port_mask = 0;
static int parse_portmask(const char *portmask);
static int stats_core = -1;

/**
 * Prints out usage information to stdout
 */
static void
usage(void)
{
	printf(
	    "%s [EAL options] -- -p PORTMASK --stats_core STATS_CORE --stats UPDATE_TIME\n"
	    " -p PORTMASK: hexadecimal bitmask of ports to use\n"
	    " --stats_core CORE_ID: the core used to display stats\n"
	    " --stats UPDATE_TIME:\n"
	    "   Interval (in seconds) at which stats are updated. Set to 0 to disable (default)\n"
	    , progname);
}

/**
 * The application specific arguments follow the DPDK-specific
 * arguments which are stripped by the DPDK init. This function
 * processes these application arguments, printing usage info
 * on error.
 */
int
ovdk_args_parse_app_args(int argc, char *argv[])
{
	int option_index, opt;
	char **argvopt = argv;
	static struct option lgopts[] = {
			{PARAM_STATS, 1, 0, 0},
			{PARAM_STATS_CORE, 1, 0, 0},
			{NULL, 0, 0, 0}
	};

	progname = argv[0];

	while ((opt = getopt_long(argc, argvopt, "p:", lgopts,
		&option_index)) != EOF) {
		switch (opt) {
                        case 'p':
                                if (parse_portmask(optarg) != 0) {
                                        usage();
                                        return -1;
                                }
                                break;
			case 0:
				if (strncmp(lgopts[option_index].name, PARAM_STATS, 5) == 0) {
					stats_interval = atoi(optarg);
				} else if (strncmp(lgopts[option_index].name, PARAM_STATS_CORE, 8) == 0) {
					stats_core = atoi(optarg);
				}
				break;
			default:
				printf("ERROR: Unknown option '%c'\n", opt);
				usage();
				return -1;
		}
	}
	return 0;
}

int
ovdk_args_get_stats_interval(void) {
	return stats_interval;
}

uint64_t
ovdk_args_get_portmask(void){
	return port_mask;
}

static int
parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	if (portmask == NULL || *portmask == '\0')
		return -1;

	/* convert parameter to a number and verify */
	pm = strtoul(portmask, &end, PORTMASK_BASE);
	if (end == NULL || *end != '\0' || pm == 0)
		return -1;
	port_mask = pm;

	return 0;
}

int
ovdk_args_get_stats_core(void) {
	return stats_core;
}

