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
#include <stdint.h>
#include <getopt.h>
#include <string.h>

#include <rte_config.h>
#include <rte_common.h>

#include "ovdk_args.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define PARAM_STATS_INTERVAL "stats_int"
#define PARAM_STATS_CORE "stats_core"

#define PORTMASK_BASE 16

static const char *progname;
static uint64_t port_mask = 0;
static int stats_interval = 0;
static int stats_core = -1;

static int parse_portmask(const char *portmask);

/*
 * Display usage instructions.
 */
void
ovdk_args_usage(const char *name)
{
	printf(
	    "%s: Intel DPDK vSwitch datapath application\n"
	    "usage: %s [EAL] -- [ARG...]\n"
	    "\n"
	    "Required Arguments:\n"
	    "  -p PORTMASK                 hex bitmask of phy ports to use\n"
	    "\n"
	    "Optional Arguments:\n"
	    "  --stats_int SECS            print stats every SECS seconds (default: 0)\n"
	    "  --stats_core CORE           id of core used to print stats\n",
	    name, name);
}

/*
 * Parse application arguments.
 *
 * The application specific arguments succeed the DPDK-specific arguments,
 * which are stripped by the DPDK EAL init. Process these application
 * arguments, and display the usage info on error.
 */
int
ovdk_args_parse_app_args(int argc, char *argv[])
{
	int option_index, opt;
	char **argvopt = argv;
	static struct option lgopts[] = {
		{PARAM_STATS_INTERVAL, required_argument, NULL, 0},
		{PARAM_STATS_CORE, required_argument, NULL, 0},
		{NULL, 0, NULL, 0}
	};

	progname = argv[0];

	while ((opt = getopt_long(argc, argvopt, "p:", lgopts, &option_index))
	       != EOF) {
		switch (opt) {
		case 'p':
			if (parse_portmask(optarg) != 0) {
				ovdk_args_usage(progname);
				rte_exit(EXIT_FAILURE, "Invalid option"
				         " specified '%c'\n", opt);
			}
			break;
		case 0:
			if (strncmp(lgopts[option_index].name,
			            PARAM_STATS_INTERVAL, 9) == 0)
				stats_interval = atoi(optarg);
			else if (strncmp(lgopts[option_index].name,
			                 PARAM_STATS_CORE, 10) == 0)
				stats_core = atoi(optarg);
			break;
		case '?':
		default:
			ovdk_args_usage(progname);
			if (optopt)
				rte_exit(EXIT_FAILURE, "Invalid option '-%c'"
				         "\n", optopt);
			else
				rte_exit(EXIT_FAILURE, "Invalid option '--%s'"
				         "\n", argv[optind - 1]);
		}
	}

	return 0;
}

/*
 * Parse the supplied portmask argument.
 *
 * This does not actually validate the port bitmask - it merely parses and
 * stores it. As a result, validation must be carried out on this value.
 */
static int
parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long num = 0;

	num = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	port_mask = num;

	return 0;
}

uint64_t
ovdk_args_get_portmask(void){
	return port_mask;
}

int
ovdk_args_get_stats_interval(void) {
	return stats_interval;
}

int
ovdk_args_get_stats_core(void) {
	return stats_core;
}

