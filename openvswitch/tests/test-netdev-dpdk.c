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
#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_memzone.h>
#include <rte_ether.h>
#include <rte_byteorder.h>
#include <linux/openvswitch.h>
#include <getopt.h>

#include "common.h"
#include "netdev-provider.h"
#include "packets.h"
#include "vlog.h"
#include "netdev-dpdk.h"
#include "dpif-dpdk.h"

#include <string.h>
#include <assert.h>


void test_netdev_dpdk_change_seq(struct netdev *netdev_p);

int
main(int argc, char *argv[])
{
	struct netdev netdev;
	struct netdev *netdev_p = &netdev;
	struct netdev **netdevp = &netdev_p;
	int c = 0;

	netdev_open("br0", "dpdkphy", netdevp);

	while(1)
	{
		static struct option long_options[] =
		{
			{"netdev_dpdk_test_change_seq", no_argument, 0, 'a'},
			{0, 0, 0, 0}
		};
		int option_index = 0;
		c = getopt_long(argc, argv, "a", long_options, &option_index);

		if (c == -1)
			break;

		switch (c)
		{
			case 'a':
			test_netdev_dpdk_change_seq(netdev_p);
			break;

			default:
			abort();
		}
	}

	netdev_close(netdev_p);
	return 0;
}

void test_netdev_dpdk_change_seq(struct netdev *netdev_p)
{
	int result = -1;

	result = netdev_p->netdev_class->change_seq(netdev_p);
	assert(result == 0);
}
