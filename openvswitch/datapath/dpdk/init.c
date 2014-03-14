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

#include <string.h>
#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_memzone.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include "jobs.h"
#include "kni.h"
#include "init_drivers.h"
#include "flow.h"
#include "args.h"
#include "init.h"
#include "main.h"
#include "vport.h"
#include "datapath.h"
#include "stats.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NO_FLAGS 0

/* These are used to dimension the overall size of the mbuf mempool. They
 * are arbitrary values that have been determined by tuning */
#define MBUFS_PER_CLIENT  3072
#define MBUFS_PER_PORT    3072
#define MBUFS_PER_KNI     3072
#define MBUFS_PER_VETH    3072
#define MBUFS_PER_VHOST   3072
#define MBUFS_PER_MEMNIC  3072
#define MBUFS_PER_DAEMON  2048

#define MBUF_CACHE_SIZE 128
#define MBUF_OVERHEAD (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define MAX_PACKET_SIZE 1520
#define MBUF_SIZE (MAX_PACKET_SIZE + MBUF_OVERHEAD + \
		RTE_MAX(sizeof(struct dpdk_message),sizeof(struct dpdk_upcall)))

/**
 * Initialise the mbuf pool
 */
static int
init_mbuf_pools(void)
{
	const unsigned num_mbufs = (num_clients * MBUFS_PER_CLIENT)
			+ (port_cfg.num_phy_ports * MBUFS_PER_PORT)
			+ (num_kni * MBUFS_PER_KNI)
			+ (num_veth * MBUFS_PER_VETH)
			+ (num_vhost * MBUFS_PER_VHOST)
			+ (num_memnic * MBUFS_PER_MEMNIC)
			+ MBUFS_PER_DAEMON;

	/* make sure the upcall does not the exceed mbuf headroom */
	if (sizeof(struct dpdk_upcall) >= RTE_PKTMBUF_HEADROOM)
		rte_panic("Upcall exceed mbuf headroom\n");

	/* don't pass single-producer/single-consumer flags to mbuf create as it
	 * seems faster to use a cache instead */
	printf("Creating mbuf pool '%s' [%u mbufs] ...\n",
			PKTMBUF_POOL_NAME, num_mbufs);
	pktmbuf_pool = rte_mempool_create(PKTMBUF_POOL_NAME, num_mbufs,
			MBUF_SIZE, MBUF_CACHE_SIZE,
			sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init,
			NULL, rte_pktmbuf_init, NULL, SOCKET0, NO_FLAGS );

	return (pktmbuf_pool == NULL); /* 0  on success */
}

/**
 * Main init function for the multi-process server app,
 * calls subfunctions to do each stage of the initialisation.
 */
int
init(int argc, char *argv[])
{
	int retval;
	unsigned lcore;
	uint8_t total_ports = 0;

	/* init EAL, parsing EAL args */
	retval = rte_eal_init(argc, argv);
	if (retval < 0)
		return -1;
	argc -= retval;
	argv += retval;
        if (rte_eal_pci_probe())
                rte_panic("Cannot probe PCI\n");

	/* initialise the nic drivers */
	retval = init_drivers();
	if (retval != 0)
		rte_exit(EXIT_FAILURE, "Cannot initialise drivers\n");

	/* get total number of ports */
	total_ports = rte_eth_dev_count();

	/* parse additional, application arguments */
	retval = parse_app_args(total_ports, argc, argv);
	if (retval != 0)
		return -1;

	/* initialise mbuf pools */
	retval = init_mbuf_pools();
	if (retval != 0)
		rte_exit(EXIT_FAILURE, "Cannot create needed mbuf pools\n");

	jobs_init();
	flow_table_init();
	datapath_init();
	vport_init();
	stats_init();
	if (num_vhost)
		vhost_init();

	/* Set flags on each core for safe vhost device removal. */
	RTE_LCORE_FOREACH(lcore) {
		dev_removal_flag[lcore] = ACK_DEV_REMOVAL;
	}
	return 0;
}

