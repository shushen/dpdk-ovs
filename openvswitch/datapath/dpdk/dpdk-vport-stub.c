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

#include "vport.h"

#define MAX_BUFS             100
#define MAX_PORT_TYPES       3
#define MAX_PORTS_PER_CLIENT 16

/* port types */
#define RING     0
#define KNI      1
#define PHY      2
#define VETH     3

struct rte_mbuf *buf_array[MAX_PORT_TYPES][MAX_PORTS_PER_CLIENT][MAX_BUFS] = {NULL};

int buf_tail[MAX_PORT_TYPES][MAX_PORTS_PER_CLIENT] = {0};
int buf_head[MAX_PORT_TYPES][MAX_PORTS_PER_CLIENT] = {0};

uint16_t dequeue(uint8_t portid, uint8_t type, struct rte_mbuf **bufs);
int enqueue(uint8_t portid, uint8_t type, struct rte_mbuf *buf);

uint16_t
dequeue(uint8_t portid, uint8_t type, struct rte_mbuf **bufs)
{
	int count = 0;
	int i = 0;
	int head = buf_head[type][portid];
	int tail = buf_tail[type][portid];

	/* check how many buffers can be received */
	count = tail - head;

	/* only receive PKT_BURST_SIZE as maximum */
	if (count > PKT_BURST_SIZE)
		count = PKT_BURST_SIZE;

	for (i = 0; i < count; i++) {
		bufs[i] = buf_array[type][portid][head];
		head = ++buf_head[type][portid];
	}

	return count;
}


int
enqueue(uint8_t portid, uint8_t type, struct rte_mbuf *buf)
{
	int tail = buf_tail[type][portid]++;

	/* add one buffer to buf structure and update index */
	buf_array[type][portid][tail] = buf;
	return 0;
}

void vport_init(void)
{
	/* init and fini will both re-initialize buf pointers and indices */
	vport_fini();
	return;
}

void vport_fini(void)
{
	int bufs = 0;
	int port_types = 0;
	int ports = 0;

	/* initialize all buf pointers and indices to zero */
	for (bufs = 0; bufs < MAX_BUFS; bufs++)
		for (port_types = 0; port_types < MAX_PORT_TYPES; port_types++)
			for (ports = 0; ports < MAX_PORTS_PER_CLIENT; ports++) {
				buf_array[port_types][ports][bufs] = NULL;
				buf_tail[port_types][ports] = 0;
				buf_head[port_types][ports] = 0;
			}

	return;
}

int
send_to_client(uint8_t client, struct rte_mbuf *buf)
{
	return enqueue(client, RING, buf);
}

int
send_to_port(uint8_t vportid, struct rte_mbuf *buf)
{
	return enqueue(vportid & PORT_MASK , PHY, buf);
}

int
send_to_kni(uint8_t vportid, struct rte_mbuf *buf)
{
	return enqueue(vportid & KNI_MASK, KNI, buf);
}

int
send_to_veth(uint8_t vportid, struct rte_mbuf *buf)
{
	return enqueue(vportid & VETH_MASK, VETH, buf);
}

uint16_t
receive_from_veth(uint8_t vportid, struct rte_mbuf **bufs)
{
	return dequeue(vportid & VETH_MASK, VETH, bufs);
}

uint16_t
receive_from_kni(uint8_t vportid, struct rte_mbuf **bufs)
{
	return dequeue(vportid & KNI_MASK, KNI, bufs);
}

uint16_t
receive_from_client(uint8_t client, struct rte_mbuf **bufs)
{
	return dequeue(client, RING, bufs);
}

uint16_t
receive_from_port(uint8_t vportid, struct rte_mbuf **bufs)
{
	return dequeue(vportid & PORT_MASK, PHY, bufs);
}
