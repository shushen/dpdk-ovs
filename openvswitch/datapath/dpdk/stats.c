/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2013 Intel Corporation. All rights reserved.
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
#include <rte_ether.h>
#include <rte_memzone.h>

#include "stats.h"
#include "init.h"
#include "vport.h" /* for MAX_VPORTS */

#define NO_FLAGS            0
#define MZ_STATS_INFO "MProc_stats_info"
#define VPORT_STATS_SIZE (sizeof(struct vport_statistics) * MAX_VPORTS +  \
                          sizeof(struct vswitch_statistics))
//#define STATS_DISABLE

/*
 * vport statistics structure, used by both clients and kni ports
 * to record traffic information
 */
struct vport_statistics {
	volatile uint64_t rx;
	volatile uint64_t tx;
	volatile uint64_t rx_drop;
	volatile uint64_t tx_drop;
	volatile uint64_t overrun;
};

struct vswitch_statistics {
	uint64_t tx_drop;
	uint64_t rx_drop;
};

static struct vport_statistics *vport_stats[MAX_VPORTS] = {NULL};
static struct vswitch_statistics *vswitch_stats = NULL;

void
stats_clear(void)
{
	stats_vswitch_clear();
	stats_vport_clear_all();
}

/*
 * Function to set vswitch statistic values to zero.
 */
void
stats_vswitch_clear(void)
{
	vswitch_stats->rx_drop = 0;
	vswitch_stats->tx_drop = 0;
}

/*
 * Function to set vport statistic values to zero.
 */
void
stats_vport_clear(unsigned vportid)
{
	vport_stats[vportid]->rx = 0;
	vport_stats[vportid]->rx_drop = 0;
	vport_stats[vportid]->tx = 0;
	vport_stats[vportid]->tx_drop = 0;
	vport_stats[vportid]->overrun = 0;
}

/*
 * Function to set all vport statistics to zero
 */
void stats_vport_clear_all(void)
{
	unsigned vportid = 0;

	for (vportid = 0; vportid < MAX_VPORTS; vportid++) {
		stats_vport_clear(vportid);
	}
}

#ifdef STATS_DISABLE
void stats_vport_rx_increment(unsigned vportid, int inc)
{
}

void stats_vport_rx_drop_increment(unsigned vportid, int inc)
{
}

void stats_vport_tx_increment(unsigned vportid, int inc)
{
}

void stats_vport_tx_drop_increment(unsigned vportid, int inc)
{
}

void stats_vport_overrun_increment(unsigned vportid, int inc)
{
}

void stats_vswitch_rx_drop_increment(int inc)
{
}

void stats_vswitch_tx_drop_increment(int inc)
{
}

#else /* STATS_DISABLE */
void stats_vport_rx_increment(unsigned vportid, int inc)
{
	vport_stats[vportid]->rx += inc;
}

void stats_vport_rx_drop_increment(unsigned vportid, int inc)
{
	vport_stats[vportid]->rx_drop += inc;
}

void stats_vport_tx_increment(unsigned vportid, int inc)
{
	vport_stats[vportid]->tx += inc;
}

void stats_vport_tx_drop_increment(unsigned vportid, int inc)
{
	vport_stats[vportid]->tx_drop += inc;
}

void stats_vport_overrun_increment(unsigned vportid, int inc)
{
	vport_stats[vportid]->overrun += inc;
}

void stats_vswitch_rx_drop_increment(int inc)
{
	vswitch_stats->rx_drop += inc;
}

void stats_vswitch_tx_drop_increment(int inc)
{
	vswitch_stats->tx_drop += inc;
}

#endif /* STATS_DISABLE */

uint64_t stats_vport_rx_get(unsigned vportid)
{
	return vport_stats[vportid]->rx;
}

uint64_t stats_vport_rx_drop_get(unsigned vportid)
{
	return vport_stats[vportid]->rx_drop;
}

uint64_t stats_vport_tx_get(unsigned vportid)
{
	return vport_stats[vportid]->tx;
}

uint64_t stats_vport_tx_drop_get(unsigned vportid)
{
	return vport_stats[vportid]->tx_drop;
}

uint64_t stats_vport_overrun_get(unsigned vportid)
{
	return vport_stats[vportid]->overrun;
}

uint64_t stats_vswitch_rx_drop_get(void)
{
	return vswitch_stats->rx_drop;
}

uint64_t stats_vswitch_tx_drop_get(void)
{
	return vswitch_stats->tx_drop;
}

void
stats_init(void)
{
	const struct rte_memzone *mz = NULL;
	unsigned vportid = 0;
	/* set up array for statistics */
	mz = rte_memzone_reserve(MZ_STATS_INFO, VPORT_STATS_SIZE, rte_socket_id(), NO_FLAGS);
	if (mz == NULL)
		rte_exit(EXIT_FAILURE, "Cannot reserve memory zone for statistics\n");
	memset(mz->addr, 0, VPORT_STATS_SIZE);

	for (vportid = 0; vportid < MAX_VPORTS; vportid++) {
		vport_stats[vportid] = (void *)((char *)mz->addr +
				vportid * sizeof(struct vport_statistics));
	}

	vswitch_stats = (void *)((char *)mz->addr +
				MAX_VPORTS * sizeof(struct vport_statistics));
}


