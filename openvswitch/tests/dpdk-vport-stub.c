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

#include <rte_string_fns.h>

#include "vport.h"

#define MAX_BUFS               100
#define MAX_VPORTS             256

struct rte_mbuf *buf_array[MAX_VPORTS][MAX_BUFS] = {NULL};

int buf_tail[MAX_VPORTS] = {0};
int buf_head[MAX_VPORTS] = {0};

uint16_t
receive_from_vport(uint32_t portid, struct rte_mbuf **bufs)
{
	int count = 0;
	int i = 0;
	int head = buf_head[portid];
	int tail = buf_tail[portid];

	/* check how many buffers can be received */
	count = tail - head;

	/* only receive PKT_BURST_SIZE as maximum */
	if (count > PKT_BURST_SIZE)
		count = PKT_BURST_SIZE;

	for (i = 0; i < count; i++) {
		bufs[i] = buf_array[portid][head];
		head = ++buf_head[portid];
	}

	return count;
}

int
send_to_vport(uint32_t portid, struct rte_mbuf *buf)
{
	int tail = buf_tail[portid]++;

	/* add one buffer to buf structure and update index */
	buf_array[portid][tail] = buf;
	return 0;
}

void
vport_init(void)
{
	/* init and fini will both re-initialize buf pointers and indices */
	vport_fini();
	return;
}

void
vport_fini(void)
{
	int bufs = 0;
	int ports = 0;

	/* initialize all buf pointers and indices to zero */
	for (bufs = 0; bufs < MAX_BUFS; bufs++)
		for (ports = 0; ports < MAX_VPORTS; ports++) {
			buf_array[ports][bufs] = NULL;
			buf_tail[ports] = 0;
			buf_head[ports] = 0;
		}

	return;
}

void
vport_set_name(unsigned vportid, const char *fmt, ...)
{
}

char *
vport_get_name(unsigned vportid)
{
	return NULL;
}

uint32_t
vport_name_to_portid(const char *name) {
	return 0;
}

enum vport_type
vport_get_type(unsigned vportid){
	return 0;
}

uint32_t
vport_next_available_index(enum vport_type type)
{
	return 0;
}

bool
vport_id_is_valid(unsigned vportid, enum vport_type type){
	return true;
}

bool
vport_is_enabled(unsigned vportid)
{
	return 1;
}

bool
vport_exists(unsigned vportid)
{
	return  0;
}

void
vport_enable(unsigned vportid)
{
}

void
vport_disable(unsigned vportid)
{
}

static struct rte_ring *
create_ring(const char *name)
{
	struct rte_ring *r;

	r = rte_ring_create(name, 32, SOCKET_ID_ANY, 0);
	if (!r)
		abort();
	return r;
}

void
create_vport_client(struct vport_info *vport, const char *port_name)
{
	struct vport_client *client;
	char ring_name[RTE_RING_NAMESIZE];

	vport->type = VPORT_TYPE_CLIENT;
	rte_snprintf(vport->name, sizeof(vport->name), port_name);

	client = &vport->client;

	/* Create rings and store their names */
	rte_snprintf(ring_name, sizeof(ring_name), "%sRX", port_name);
	client->rx_q = create_ring(ring_name);
	rte_snprintf(client->ring_names.rx, sizeof(client->ring_names.rx), ring_name);

	rte_snprintf(ring_name, sizeof(ring_name), "%sTX", port_name);
	client->tx_q = create_ring(ring_name);
	rte_snprintf(client->ring_names.tx, sizeof(client->ring_names.tx), ring_name);

	rte_snprintf(ring_name, sizeof(ring_name), "%sFREE", port_name);
	client->free_q = create_ring(ring_name);
	rte_snprintf(client->ring_names.free, sizeof(client->ring_names.free), ring_name);
}

static const struct rte_memzone *
create_memzone(const char *name)
{
	const struct rte_memzone *mz;

	mz = rte_memzone_reserve(name, 32, SOCKET_ID_ANY, 0);
	if (!mz)
		abort();
	return mz;
}

void
create_vport_kni(struct vport_info *vport, const char *port_name)
{
	struct vport_kni *kni;
	char mz_name[RTE_MEMZONE_NAMESIZE];

	vport->type = VPORT_TYPE_KNI;
	rte_snprintf(vport->name, sizeof(vport->name), port_name);

	kni = &vport->kni;

	/* Create memzones and store their names */
	rte_snprintf(mz_name, sizeof(mz_name), "%sTX", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.tx, sizeof(kni->fifo_names.tx), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sRX", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.rx, sizeof(kni->fifo_names.rx), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sALLOC", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.alloc, sizeof(kni->fifo_names.alloc), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sFREE", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.free, sizeof(kni->fifo_names.free), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sRESP", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.resp, sizeof(kni->fifo_names.resp), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sREQ", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.req, sizeof(kni->fifo_names.req), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sSYNC", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.sync, sizeof(kni->fifo_names.sync), mz_name);
}
