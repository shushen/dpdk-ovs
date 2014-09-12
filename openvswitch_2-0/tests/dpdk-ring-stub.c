/*-
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

#include <sys/syscall.h>
#include <unistd.h>

#include <config.h>
#include <rte_config.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_string_fns.h>
#include <rte_byteorder.h>

#include "linux/openvswitch.h"
#include "datapath/dpdk/ovdk_datapath_messages.h"
#include "datapath/dpdk/ovdk_action_types.h"
#include "flow.h"
#include "odp-util.h"
#include "netlink.h"
#include "dpdk-ring-stub.h"
#include "dpdk-flow-stub.h"
#include "dpdk-link.h"
#include "dpif-provider.h"
#include "dpif-dpdk.h"

#define VSWITCHD_RINGSIZE       2048
#define NO_FLAGS                0
#define SOCKET0                 0
#define RTE_LOGTYPE_APP         RTE_LOGTYPE_USER1
#define NUM_ALLOC_MBUFS         10
#define BUF_SIZE                64
#define MAX_PACKET_SIZE         1518

struct flow flow;
struct nlattr actions_unit;
struct dpif_flow_stats flow_stats;

struct rte_ring *vswitchd_request_ring[NUM_CORES]       = {0};
struct rte_ring *vswitchd_reply_ring[NUM_CORES]         = {0};
struct rte_ring *vswitchd_packet_ring[NUM_CORES]        = {0};
struct rte_ring *vswitchd_exception_ring[NUM_CORES]     = {0};
struct rte_ring *vswitchd_packet_free_ring[NUM_CORES]   = {0};
struct rte_ring *vswitchd_control_free_ring[NUM_CORES]  = {0};
struct rte_ring *vswitchd_packet_alloc_ring[NUM_CORES]  = {0};
struct rte_ring *vswitchd_control_alloc_ring[NUM_CORES] = {0};

static struct rte_ring *ring_create(const char *template, uint8_t lcore_id);
static void create_dpif_flow(struct ofpbuf *buf);
static void create_actions(struct ofpbuf *buf);
static int is_core_enabled(int core, uint64_t mask);

static void
create_actions(struct ofpbuf *buf)
{
	/* Create a fake arbitrary set of actions */
	nl_msg_put_u32(buf, OVS_ACTION_ATTR_OUTPUT, DPDK_RING_STUB_ACTION_OUTPUT_0);
	nl_msg_put_u32(buf, OVS_ACTION_ATTR_OUTPUT, DPDK_RING_STUB_ACTION_OUTPUT_1);
	nl_msg_put_flag(buf, OVS_ACTION_ATTR_POP_VLAN);
	nl_msg_put_u32(buf, OVS_ACTION_ATTR_OUTPUT, DPDK_RING_STUB_ACTION_OUTPUT_2);
}

static void
create_dpif_flow(struct ofpbuf *buf)
{
	/* Flow key */
	/* OVS 2.0 doesn't take in_port from the flow struct, instead it's
	 * passed as a third parameter. This is to allow handling both OF
	 * ports and datapath ports.
	 */
	memset(&flow, 0, sizeof(flow));
	flow.in_port.odp_port = DPDK_RING_STUB_IN_PORT;
	flow.nw_proto = DPDK_RING_STUB_NW_PROTO;
	flow.tp_src = rte_cpu_to_be_16(DPDK_RING_STUB_TP_SRC);
	flow.tp_dst = rte_cpu_to_be_16(DPDK_RING_STUB_TP_DST);
	flow.nw_src = rte_cpu_to_be_32(DPDK_RING_STUB_NW_SRC);
	flow.nw_dst = rte_cpu_to_be_32(DPDK_RING_STUB_NW_DST);
	flow.vlan_tci = rte_cpu_to_be_16(DPDK_RING_STUB_VLAN_TCI);
	flow.dl_dst[0] = DPDK_RING_STUB_DL_DST_0;
	flow.dl_dst[1] = DPDK_RING_STUB_DL_DST_1;
	flow.dl_dst[2] = DPDK_RING_STUB_DL_DST_2;
	flow.dl_dst[3] = DPDK_RING_STUB_DL_DST_3;
	flow.dl_dst[4] = DPDK_RING_STUB_DL_DST_4;
	flow.dl_dst[5] = DPDK_RING_STUB_DL_DST_5;
	flow.dl_src[0] = DPDK_RING_STUB_DL_SRC_0;
	flow.dl_src[1] = DPDK_RING_STUB_DL_SRC_1;
	flow.dl_src[2] = DPDK_RING_STUB_DL_SRC_2;
	flow.dl_src[3] = DPDK_RING_STUB_DL_SRC_3;
	flow.dl_src[4] = DPDK_RING_STUB_DL_SRC_4;
	flow.dl_src[5] = DPDK_RING_STUB_DL_SRC_5;
	flow.dl_type = rte_cpu_to_be_16(DPDK_RING_STUB_DL_TYPE);

	odp_flow_key_from_flow(buf, &flow, DPDK_RING_STUB_IN_PORT);
}

void
create_dpif_flow_get_message(struct dpif_flow_put *get)
{
	struct ofpbuf *buf;

	buf = ofpbuf_new(BUF_SIZE);

	create_dpif_flow(buf);
	get->key = buf->data;
	get->key_len = buf->size;
}

void
create_dpdk_flow_get_reply(struct ovdk_message *reply)
{
	memset(reply, 0, sizeof(*reply));

	reply->type = OVDK_FLOW_CMD_FAMILY;
	reply->error = 0;
	reply->flow_msg.thread_id = (uint32_t)syscall(SYS_gettid);

	/* Set stats */
	reply->flow_msg.stats.packet_count = DPDK_FLOW_STUB_STATS_PACKET_COUNT;
	reply->flow_msg.stats.byte_count   = DPDK_FLOW_STUB_STATS_BYTE_COUNT;
	reply->flow_msg.stats.used         = DPDK_FLOW_STUB_STATS_USED;
	reply->flow_msg.stats.tcp_flags    = DPDK_FLOW_STUB_STATS_TCP_FLAGS;
}

void
create_dpif_flow_put_message(struct dpif_flow_put *put)
{
	struct ofpbuf *key_buf;
	struct ofpbuf *action_buf;

	key_buf = ofpbuf_new(BUF_SIZE);
	action_buf = ofpbuf_new(BUF_SIZE);

	/* Output action */
	create_actions(action_buf);
	put->actions_len = action_buf->size;
	put->actions = action_buf->data;

	create_dpif_flow(key_buf);
	put->key = key_buf->data;
	put->key_len = key_buf->size;

	/* Flags */
	put->flags = DPIF_FP_CREATE;

	/* Set stats to NULL */
	put->stats = NULL;
}

void
create_dpdk_flow_put_reply(struct ovdk_message *reply, int error)
{
	memset(reply, 0, sizeof(*reply));
	reply->type = OVDK_FLOW_CMD_FAMILY;
	reply->error = error;
	reply->flow_msg.thread_id = (uint32_t)syscall(SYS_gettid);
	reply->flow_msg.flow_handle = DPDK_RING_STUB_FLOW_HANDLE;
}

void
create_dpdk_flow_del_reply(struct ovdk_message *reply)
{
	create_dpdk_flow_put_reply(reply, 0);
	reply->flow_msg.stats.packet_count = DPDK_FLOW_STUB_STATS_PACKET_COUNT;
	reply->flow_msg.stats.byte_count   = DPDK_FLOW_STUB_STATS_BYTE_COUNT;
	reply->flow_msg.stats.used         = DPDK_FLOW_STUB_STATS_USED;
	reply->flow_msg.stats.tcp_flags    = DPDK_FLOW_STUB_STATS_TCP_FLAGS;
}

void
create_dpif_execute_message(struct dpif_execute *execute)
{
	struct ofpbuf *buf = NULL;
	struct ofpbuf *actions_buf = NULL;

	buf = ofpbuf_new(64);
	actions_buf = ofpbuf_new(1000);

	/* Output action */
	create_actions(actions_buf);
	execute->actions_len = actions_buf->size;
 	execute->actions = actions_buf->data;

	create_dpif_flow(buf);
	execute->key = buf->data;
	execute->key_len = buf->size;

	execute->packet = ofpbuf_new(MAX_PACKET_SIZE);
}


void
create_dpif_flow_del_message(struct dpif_flow_del *del)
{
	struct ofpbuf *buf;

	buf = ofpbuf_new(BUF_SIZE);

	/* Key */
	create_dpif_flow(buf);
	del->key = buf->data;
	del->key_len = buf->size;

	del->stats = &flow_stats;
}

void
create_dpdk_port_reply(struct ovdk_message *reply,
                       int return_code,
                       uint32_t flags)
{
	struct ovdk_vport_message vport_msg = {0};

	memset(reply, 0, sizeof(*reply));

	reply->vport_msg = vport_msg;
	reply->vport_msg.thread_id = (uint32_t)syscall(SYS_gettid);
	reply->vport_msg.flags = flags;
	reply->type = OVDK_VPORT_CMD_FAMILY;
	reply->error = return_code;
}

/* Put an ovdk_message on the reply ring, ready to be dequeued by
 * flow_transact */
int
enqueue_reply_on_reply_ring(struct ovdk_message reply, unsigned pipeline_id)
{
	struct rte_mbuf *mbuf = NULL;
	void *ctrlmbuf_data = NULL;
	int rslt = 0;

	if (rte_ring_mc_dequeue(vswitchd_control_alloc_ring[pipeline_id], (void**)&mbuf) != 0)
		return -1;

	ctrlmbuf_data = rte_ctrlmbuf_data(mbuf);
	rte_memcpy(ctrlmbuf_data, &reply, sizeof(reply));

	rslt = rte_ring_mp_enqueue(vswitchd_reply_ring[pipeline_id], (void *)mbuf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_ring_mp_enqueue(vswitchd_control_free_ring[pipeline_id], mbuf);
			return -1;
		}
		return 0;
	}

	return 0;
}

/* Put an upcall on the exception ring */
int
enqueue_upcall_on_exception_ring(uint8_t upcall_cmd, unsigned pipeline_id)
{
	struct rte_mbuf *mbuf = NULL;
	void *pktmbuf_data = NULL;
	struct ovdk_upcall upcall = {0};
	int rslt = 0;

	if (rte_ring_mc_dequeue(vswitchd_packet_alloc_ring[pipeline_id], (void**)&mbuf) != 0)
		return -1;

	upcall.cmd = upcall_cmd;
	upcall.key.in_port = DPDK_RING_STUB_IN_PORT;
	upcall.key.ip_dst = rte_cpu_to_be_32(DPDK_RING_STUB_NW_DST);
	upcall.key.ip_src = rte_cpu_to_be_32(DPDK_RING_STUB_NW_SRC);
	upcall.key.ip_proto = DPDK_RING_STUB_NW_PROTO;
	upcall.key.ether_type = rte_cpu_to_be_16(DPDK_RING_STUB_DL_TYPE);
	upcall.key.tran_src_port = rte_cpu_to_be_16(DPDK_RING_STUB_TP_SRC);
	upcall.key.tran_dst_port = rte_cpu_to_be_16(DPDK_RING_STUB_TP_DST);

	rte_pktmbuf_data_len(mbuf) = MAX_PACKET_SIZE;
	rte_pktmbuf_pkt_len(mbuf) = rte_pktmbuf_data_len(mbuf);

	pktmbuf_data = rte_pktmbuf_prepend(mbuf, sizeof(struct ovdk_upcall));
	rte_memcpy(pktmbuf_data, &upcall, sizeof(struct ovdk_upcall));

	rslt = rte_ring_mp_enqueue(vswitchd_exception_ring[pipeline_id], (void *)mbuf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_ring_mp_enqueue(vswitchd_packet_free_ring[pipeline_id], mbuf);
			return -1;
		}
		return 0;
	}

	return 0;
}

int
dequeue_request_from_request_ring(struct ovdk_message **request, unsigned pipeline_id)
{
	struct rte_mbuf *mbuf = NULL;

	if (rte_ring_mc_dequeue(vswitchd_request_ring[pipeline_id], (void **)&mbuf) != 0)
		return -1;

	*request = rte_ctrlmbuf_data(mbuf);

	return 0;
}

int
dequeue_packet_from_packet_ring(struct ovdk_action *actions, uint8_t *num_actions, unsigned pipeline_id)
{
	struct rte_mbuf *mbuf = NULL;
	struct ovdk_message *message = NULL;
	int i = 0;

	if (rte_ring_mc_dequeue(vswitchd_packet_ring[pipeline_id], (void **)&mbuf) != 0)
		return -1;

	message = rte_pktmbuf_mtod(mbuf, struct ovdk_message *);

	if (message->type != OVDK_PACKET_CMD_FAMILY)
		return -1;

	*num_actions = message->packet_msg.num_actions;

	for (i = 0; i < *num_actions; i++)
		actions[i] = message->packet_msg.actions[i];

	return 0;
}

static int
is_core_enabled(int core, uint64_t mask)
{
	return ((mask >> core) & 1);
}

/*
 * dpdk_link_send() looks up each of these rings and will exit if
 * it doesn't find them so we must declare them.
 *
 * We have to call dpdk_link_send to initialise the "mp" pktmbuf pool
 * pointer used throughout dpdk_link.c
 */
void
init_test_rings(unsigned mempool_size, uint64_t *mask)
{
	int i = 0;
	int j = 0;
	struct rte_mbuf *mbuf;
	struct rte_mempool *ctrlmbuf_pool = NULL;
	struct rte_mempool *pktmbuf_pool = NULL;
	uint64_t dpdk_link_mask = 0;

	ctrlmbuf_pool = rte_mempool_create("MProc_ctrlmbuf_pool",
	                     mempool_size, /* num mbufs */
	                     2048 + sizeof(struct rte_mbuf) + 128,
	                     0, /*cache size */
	                     0,
	                     NULL,
	                     NULL, rte_ctrlmbuf_init, NULL, 0, 0);

	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
	                     mempool_size, /* num mbufs */
	                     2048 + sizeof(struct rte_mbuf) + 128,
	                     0, /*cache size */
	                     0,
	                     NULL,
	                     NULL, rte_pktmbuf_init, NULL, 0, 0);

	for (i = 0; i < NUM_CORES; i++) {
		if (is_core_enabled(i, *mask)) {
			vswitchd_exception_ring[i] = ring_create(VSWITCHD_EXCEPTION_RING_NAME, i);
			if (vswitchd_exception_ring[i] == NULL)
				rte_exit(EXIT_FAILURE,
					 "Cannot create exception ring for vswitchd");

			vswitchd_packet_ring[i] = ring_create(VSWITCHD_PACKET_RING_NAME, i);
			if (vswitchd_packet_ring[i] == NULL)
				rte_exit(EXIT_FAILURE,
					 "Cannot create packet ring for vswitchd");

			vswitchd_reply_ring[i] = ring_create(VSWITCHD_REPLY_RING_NAME, i);
			if (vswitchd_reply_ring[i] == NULL)
				rte_exit(EXIT_FAILURE,
					 "Cannot create reply ring for vswitchd");

			vswitchd_request_ring[i] = ring_create(VSWITCHD_REQUEST_RING_NAME, i);
			if (vswitchd_request_ring[i] == NULL)
				rte_exit(EXIT_FAILURE,
					 "Cannot create request ring for vswitchd");

			vswitchd_packet_free_ring[i] = ring_create(VSWITCHD_PACKET_FREE_RING_NAME, i);
			if (vswitchd_packet_free_ring[i] == NULL)
				rte_exit(EXIT_FAILURE,
					 "Cannot create packet free ring for vswitchd");

			vswitchd_control_free_ring[i] = ring_create(VSWITCHD_CONTROL_FREE_RING_NAME, i);
			if (vswitchd_control_free_ring[i] == NULL)
				rte_exit(EXIT_FAILURE,
					 "Cannot create control free ring for vswitchd");

			vswitchd_packet_alloc_ring[i] = ring_create(VSWITCHD_PACKET_ALLOC_RING_NAME, i);
			if (vswitchd_packet_alloc_ring[i] == NULL)
				rte_exit(EXIT_FAILURE,
					 "Cannot create packet alloc ring for vswitchd");

			vswitchd_control_alloc_ring[i] = ring_create(VSWITCHD_CONTROL_ALLOC_RING_NAME, i);
			if (vswitchd_control_alloc_ring[i] == NULL)
				rte_exit(EXIT_FAILURE,
					 "Cannot create control alloc ring for vswitchd");

			/* Populate the alloc queues with mbufs from the mbuf mempools */
			for (j = 0; j < mempool_size/NUM_CORES; j++) {
				if ((mbuf = rte_pktmbuf_alloc(pktmbuf_pool)) != NULL)
					rte_ring_sp_enqueue(vswitchd_packet_alloc_ring[i], mbuf);

				if ((mbuf = rte_ctrlmbuf_alloc(ctrlmbuf_pool)) != NULL)
					rte_ring_sp_enqueue(vswitchd_control_alloc_ring[i], mbuf);
			}
		}
	}

	dpdk_link_init(&dpdk_link_mask);
	/* All of the rings in 'mask' should be found by dpdk_link - if not, something has gone wrong. */
	if (dpdk_link_mask != *mask)
		rte_exit(EXIT_FAILURE,
			 "Masks differ - exiting\n");
}

/*
 * Create an rte_ring using a known template and the lcore id
 */
static struct rte_ring *
ring_create(const char *template, uint8_t lcore_id)
{
	struct rte_ring *ring = NULL;
	char ring_name[OVDK_MAX_NAME_SIZE] = {0};
	unsigned socket_id = rte_socket_id();

	snprintf(ring_name, sizeof(ring_name), template, lcore_id);
	ring = rte_ring_create(ring_name, VSWITCHD_RINGSIZE, socket_id,
	                       NO_FLAGS);
	if (ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create ring %s", ring_name);

	RTE_LOG(INFO, APP, "Created %s\n", ring_name);
	return ring;
}
