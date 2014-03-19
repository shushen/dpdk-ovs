/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2013-2014 NEC All rights reserved.
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

#include <stdint.h>

#include <rte_mbuf.h>
#include <rte_memcpy.h>

#include <assert.h>

#include "test-memnic.h"
#include "vport.h"
#include "vport-memnic.h"
#include "memnic.h"

struct rte_mempool *pktmbuf_pool;

static struct vport_memnic memnic;

static void ut_memnic_init(void)
{
	struct rte_mbuf *bufs[32];
	uint16_t rx;

	init_memnic_port(&memnic, MEMNIC0);

	/* reset */
	memnic.ptr->hdr.reset = 1;
	rx = memnic_rx(&memnic, MEMNIC0, bufs);
	assert(rx == 0);
}

static void ut_pktmbuf_init(void)
{
	/* create temporary pktmbuf for MEMNIC module */
	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
			20, 4096, 32, sizeof(struct rte_pktmbuf_pool_private),
			rte_pktmbuf_pool_init, NULL,
			rte_pktmbuf_init, NULL, 0, 0);
}

static void ut_initialize(void)
{
	ut_memnic_init();
	ut_pktmbuf_init();
	stats_init();
}

void test_memnic_simple_tx(int argc, char *argv[])
{
	struct rte_mbuf *mbuf;
	struct memnic_data *up;
	struct memnic_packet *p;
	uint8_t src[8] = { 0, 1, 2, 3, 7, 6, 5, 4 };

	ut_initialize();

	mbuf = rte_pktmbuf_alloc(pktmbuf_pool);

	assert(mbuf != NULL);

	rte_memcpy(rte_pktmbuf_mtod(mbuf, void *), src, 8);
	rte_pktmbuf_data_len(mbuf) = 8;
	rte_pktmbuf_pkt_len(mbuf) = 8;

	assert(memnic_tx(&memnic, MEMNIC0, mbuf) == 0);

	/* check the data in shared memory */
	up = &memnic.ptr->up;
	p = &up->packets[0];
	assert(p->status == MEMNIC_PKT_ST_FILLED);
	assert(p->len == 8);
	assert(memcmp(src, p->data, 8) == 0);
}

void test_memnic_simple_rx(int argc, char *argv[])
{
	struct rte_mbuf *bufs[32];
	struct memnic_data *down;
	struct memnic_packet *p;
	uint8_t src[8] = { 0, 1, 2, 3, 7, 6, 5, 4 };

	ut_initialize();

	down = &memnic.ptr->down;
	p = &down->packets[0];

	rte_memcpy(p->data, src, 8);
	p->len = 8;

	/* not used, returns 0 */
	p->status = MEMNIC_PKT_ST_FREE;
	assert(memnic_rx(&memnic, MEMNIC0, bufs) == 0);

	/* used but still not filled, returns 0 */
	p->status = MEMNIC_PKT_ST_USED;
	assert(memnic_rx(&memnic, MEMNIC0, bufs) == 0);

	/* got a packet */
	p->status = MEMNIC_PKT_ST_FILLED;
	assert(memnic_rx(&memnic, MEMNIC0, bufs) == 1);

	/* check the received packet */
	assert(rte_pktmbuf_data_len(bufs[0]) == 8);
	assert(rte_pktmbuf_pkt_len(bufs[0]) == 8);
	assert(memcmp(src, rte_pktmbuf_mtod(bufs[0], void *), 8) == 0);
}
