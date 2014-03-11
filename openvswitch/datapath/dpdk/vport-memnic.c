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

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <rte_ethdev.h>
#include <rte_mempool.h>

#include <rte_atomic.h>

#include "init.h"
#include "stats.h"
#include "vport.h"
#include "vport-memnic.h"

#include "memnic.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define MEMNIC_SHM_NAME "/ovs_dpdk_not_used_%u"

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

int init_memnic_port(struct vport_memnic *memnic, unsigned vportid)
{
	struct memnic_area *nic;
	struct memnic_header *hdr;
	int fd, ret;
	char name[64];
	uint64_t rand;

	RTE_LOG(INFO, APP, "MEMNIC initialize vport=%u\n", vportid);

	sprintf(name, MEMNIC_SHM_NAME, vportid);
	fd = shm_open(name, O_RDWR|O_CREAT, 0640);
	if (fd == -1) {
		RTE_LOG(WARNING, APP,
			"MEMNIC %u: Failed to open %s\n", vportid, name);
		return -1;
	}

	ret = -1;
	if (ftruncate(fd, MEMNIC_AREA_SIZE) == -1) {
		RTE_LOG(WARNING, APP,
			"MEMNIC %u: Failed to truncate\n", vportid);
		goto close_out;
	}

	nic = mmap(NULL, MEMNIC_AREA_SIZE,
			PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

	if (nic == MAP_FAILED) {
		RTE_LOG(WARNING, APP,
			"MEMNIC %u: Failed to mmap\n", vportid);
		goto close_out;
	}

	hdr = &nic->hdr;
	/* Invalidate first */
	hdr->valid = 0;
	if (hdr->magic != MEMNIC_MAGIC || hdr->version != MEMNIC_VERSION) {
		/* set MAGIC and VERSION */
		hdr->magic = MEMNIC_MAGIC;
		hdr->version = MEMNIC_VERSION;
		/* no extra features */
		hdr->features = 0;

		/* random MAC address generation */
		rand = rte_rand();
		rte_memcpy(hdr->mac_addr, &rand, ETHER_ADDR_LEN);
		hdr->mac_addr[0] &= ~ETHER_GROUP_ADDR;		/* clear multicast bit */
		hdr->mac_addr[0] |= ETHER_LOCAL_ADMIN_ADDR;	/* set local assignment bit */
		RTE_LOG(INFO, APP,
			"MEMNIC %u: Generate MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
			vportid,
			hdr->mac_addr[0], hdr->mac_addr[1],
			hdr->mac_addr[2], hdr->mac_addr[3],
			hdr->mac_addr[4], hdr->mac_addr[5]);
	}

	memnic->ptr = nic;
	memnic->up = 0;
	memnic->down = 0;

	ret = 0;

close_out:
	close(fd);
	return ret;
}

int memnic_tx(struct vport_memnic *memnic, unsigned vportid, struct rte_mbuf *buf)
{
	struct memnic_header *hdr = &memnic->ptr->hdr;
	struct memnic_data *up = &memnic->ptr->up;
	struct memnic_packet *p;
	uint32_t len;
	int idx, ret = -1;

	if (unlikely(!hdr->valid))
		goto drop;

	len = rte_pktmbuf_data_len(buf);
	if (len > MEMNIC_MAX_FRAME_LEN)
		goto drop;

retry:
	idx = ACCESS_ONCE(memnic->up);
	p = &up->packets[idx];
	if (unlikely(rte_atomic32_cmpset(&p->status,
			MEMNIC_PKT_ST_FREE, MEMNIC_PKT_ST_USED) == 0)) {
		/* cmpxchg failed */
		if (p->status == MEMNIC_PKT_ST_FILLED &&
				idx == ACCESS_ONCE(memnic->up)) {
			/* what we're seeing is FILLED means queue full */
			goto drop;
		}
		goto retry;
	}

	if (idx != ACCESS_ONCE(memnic->up)) {
		/*
		 * Guest freed this and got false positive,
		 * need to recover the status and retry.
		 */
		p->status = MEMNIC_PKT_ST_FREE;
		goto retry;
	}

	if (++idx >= MEMNIC_NR_PACKET)
		idx = 0;
	memnic->up = idx;

	p->len = len;
	rte_memcpy(p->data, rte_pktmbuf_mtod(buf, void *), len);
	rte_mb();
	p->status = MEMNIC_PKT_ST_FILLED;

	stats_vport_rx_increment(vportid, INC_BY_1);

	rte_atomic64_inc((rte_atomic64_t *)&up->info.pkts);
	rte_atomic64_add((rte_atomic64_t *)&up->info.bytes, len);

	ret = 0;

drop:
	if (ret)
		stats_vport_rx_drop_increment(vportid, INC_BY_1);

	rte_pktmbuf_free(buf);

	return 0;
}

uint16_t memnic_rx(struct vport_memnic *memnic, unsigned vportid, struct rte_mbuf **bufs)
{
	struct memnic_header *hdr = &memnic->ptr->hdr;
	struct memnic_data *up = &memnic->ptr->up;
	struct memnic_data *down = &memnic->ptr->down;
	int idx;
	unsigned i;

	/* check client wants to reset this interface, on initialize */
	if (unlikely(hdr->reset == 1)) {
		/* make sure, invalidate this */
		hdr->valid = 0;
		rte_mb();

		RTE_LOG(INFO, APP, "MEMINC %u reset", vportid);
		for (i = 0; i < MEMNIC_NR_PACKET; i++) {
			up->packets[i].status = MEMNIC_PKT_ST_FREE;
			down->packets[i].status = MEMNIC_PKT_ST_FREE;
		}
		memnic->up = 0;
		memnic->down = 0;

		rte_mb();
		hdr->reset = 0;
		hdr->valid = 1;
		return 0;
	}

	if (unlikely(!hdr->valid))
		return 0;

	i = 0;
	/*
	 * Assume, receiving function is single threaded, because we don't
	 * want to face packet ordering issue.
	 */
	idx = memnic->down;
	while (i < PKT_BURST_SIZE) {
		struct memnic_packet *p = &down->packets[idx];
		struct rte_mbuf *mbuf;

		if (p->status != MEMNIC_PKT_ST_FILLED)
			break;
		if (p->len > MEMNIC_MAX_FRAME_LEN) {
			stats_vport_tx_drop_increment(vportid, INC_BY_1);
			goto next;
		}

		mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
		if (!mbuf)
			break;
		rte_memcpy(rte_pktmbuf_mtod(mbuf, void *), p->data, p->len);
		mbuf->pkt.nb_segs = 1;
		mbuf->pkt.next = NULL;
		mbuf->pkt.pkt_len = p->len;
		mbuf->pkt.data_len = p->len;
		bufs[i] = mbuf;
		++i;
next:
		/* clear status */
		rte_mb();
		p->status = MEMNIC_PKT_ST_FREE;
		if (++idx >= MEMNIC_NR_PACKET)
			idx = 0;
	}

	memnic->down = idx;

	if (i > 0)
		stats_vport_tx_increment(vportid, i);

	return i;
}
