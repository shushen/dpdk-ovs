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

#include <config.h>
#include <string.h>
#include <assert.h>

#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_memzone.h>
#include <rte_ether.h>
#include <rte_byteorder.h>

#include "command-line.h"
#include "timeval.h"
#include "util.h"
#include "vlog.h"

#include "netdev-provider.h"
#include "packets.h"
#include "netdev-dpdk.h"
#include "dpif-dpdk.h"

struct netdev netdev;
struct netdev *netdev_p = &netdev;
struct netdev **netdevp = &netdev_p;

void test_netdev_dpdk_change_seq(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);

void
test_netdev_dpdk_change_seq(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	result = netdev_p->netdev_class->change_seq(netdev_p);
	assert(result == 0);
}

static const struct command commands[] = {
	{"change-seq", 0, 0, test_netdev_dpdk_change_seq},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	set_program_name(argv[0]);
	vlog_set_levels(NULL, VLF_ANY_FACILITY, VLL_EMER);
	vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

	time_alarm(10);

	/* Initialise system */
	netdev_open("br0", "dpdkphy", netdevp);

	run_command(argc - 1, argv + 1, commands);

	/* Cleanup system */
	netdev_close(netdev_p);

	return 0;
}
