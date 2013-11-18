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

#include <rte_config.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include "ofpbuf.h"

/* We declare a struct ofpbuf per core as each thread may do an action
 * simultaneously and while the ofbuf itself is merely overlayed on top
 * of the mbuf, the actual struct that stores the pointers may be
 * overwritten by another thread.
 *
 * This workaround is temporary
 */
struct ofpbuf buf[RTE_MAX_LCORE];

void * overlay_ofpbuf(struct rte_mbuf *mbuf)
{
	const unsigned id = rte_lcore_id();

	ofpbuf_use_const(&buf[id], mbuf->buf_addr, mbuf->buf_len);
	buf[id].data = mbuf->pkt.data;
	buf[id].size = mbuf->pkt.pkt_len;

	/* VLAN pop library function requires l2 pointer */
	buf[id].l2 = mbuf->pkt.data;

	return &buf[id];
}

void update_mbuf(struct ofpbuf *ovs_pkt, struct rte_mbuf *mbuf)
{
	/* Update mbuf pointers following action execution */
	mbuf->pkt.data = ovs_pkt->data;
	mbuf->pkt.data_len = ovs_pkt->size;
	mbuf->pkt.pkt_len = ovs_pkt->size;
}
