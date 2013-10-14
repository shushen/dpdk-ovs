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

#include <stdint.h>
#include <rte_memcpy.h>
#include <rte_ring.h>

#include "datapath.h"
#include "action.h"
#include "stats.h"
#include "init.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NO_FLAGS            0
#define SOCKET0             0
#define PKT_BURST_SIZE      32
#define VSWITCHD_RINGSIZE   2048
#define VSWITCHD_PACKET_RING_NAME  "MProc_Vswitchd_Packet_Ring"
#define VSWITCHD_REPLY_RING_NAME   "MProc_Vswitchd_Reply_Ring"
#define VSWITCHD_MESSAGE_RING_NAME "MProc_Vswitchd_Message_Ring"
/* Flow messages flags bits */
#define FLAG_ROOT              0x100
#define FLAG_MATCH             0x200
#define FLAG_ATOMIC            0x400
#define FLAG_DUMP              (FLAG_ROOT | FLAG_MATCH)

#define FLAG_REPLACE           0x100
#define FLAG_EXCL              0x200
#define FLAG_CREATE            0x400
#define FLAG_APPEND            0x800

#define FLOW_CMD_FAMILY        0xF
#define PACKET_CMD_FAMILY      0x1F


struct dpdk_flow_message {
	uint8_t cmd;
	uint32_t flags;
	struct flow_key key;
	struct flow_stats stats;
	uint32_t action;
	bool clear;
};

struct dpdk_packet_message {
	uint32_t action;  /* What to do with the packet received from
	                     the daemon. */
};

struct dpdk_message {
	int16_t type;
	union {
		struct dpdk_flow_message flow_msg;
		struct dpdk_packet_message packet_msg;
	};
};
/* ring to send packets to vswitchd */
static struct rte_ring *vswitchd_packet_ring = NULL;
/* ring to receive messages from vswitchd */
static struct rte_ring *vswitchd_message_ring = NULL;
/* ring to send reply messages to vswitchd */
static struct rte_ring *vswitchd_reply_ring = NULL;

static void send_reply_to_vswitchd(struct dpdk_message *reply);

static void handle_vswitchd_cmd(struct rte_mbuf *mbuf);
static void handle_flow_cmd(struct dpdk_flow_message *request);
static void handle_packet_cmd(struct dpdk_packet_message *request,
                              struct rte_mbuf *pkt);
static void handle_unknown_cmd(void);

static void flow_cmd_get(struct dpdk_flow_message *request);
static void flow_cmd_new(struct dpdk_flow_message *request);
static void flow_cmd_del(struct dpdk_flow_message *request);
static void flow_cmd_dump(struct dpdk_flow_message *request);

/*
 * Function sends unmatched packets to vswitchd.
 */
void
send_packet_to_vswitchd(struct rte_mbuf *mbuf, struct dpdk_upcall *info)
{
	int rslt = 0;
	void *mbuf_ptr = NULL;

	/* send one packet, delete information about segments */
	rte_pktmbuf_pkt_len(mbuf) = rte_pktmbuf_data_len(mbuf);

	/* allocate space before the packet for the upcall info */
	mbuf_ptr = rte_pktmbuf_prepend(mbuf, sizeof(*info));

	if (mbuf_ptr == NULL) {
		printf("Cannot prepend upcall info\n");
		rte_pktmbuf_free(mbuf);
		stats_vswitch_tx_drop_increment(INC_BY_1);
		stats_vport_tx_drop_increment(VSWITCHD, INC_BY_1);
		return;
	}

	rte_memcpy(mbuf_ptr, info, sizeof(*info));

	/* send the packet and the upcall info to the daemon */
	rslt = rte_ring_mp_enqueue(vswitchd_packet_ring, mbuf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_pktmbuf_free(mbuf);
			stats_vswitch_tx_drop_increment(INC_BY_1);
			stats_vport_tx_drop_increment(VSWITCHD, INC_BY_1);
			return;
		} else {
			stats_vport_overrun_increment(VSWITCHD, INC_BY_1);
		}
	}

	stats_vport_tx_increment(VSWITCHD, INC_BY_1);
}

/*
 * Function handles messages from the daemon.
 */
void
handle_request_from_vswitchd(void)
{
	int j = 0;
	uint16_t dq_pkt = PKT_BURST_SIZE;
	struct rte_mbuf *buf[PKT_BURST_SIZE] = {0};

	/* Attempt to dequeue maximum available number of mbufs from ring */
	while (dq_pkt > 0 &&
	       unlikely(rte_ring_sc_dequeue_bulk(
	       vswitchd_message_ring, (void **)buf, dq_pkt) != 0))
		dq_pkt = (uint16_t)RTE_MIN(
		   rte_ring_count(vswitchd_message_ring), PKT_BURST_SIZE);

	/* Update number of packets transmitted by daemon */
	stats_vport_rx_increment(VSWITCHD, dq_pkt);

	for (j = 0; j < dq_pkt; j++) {
		handle_vswitchd_cmd(buf[j]);
	}
}

/*
 * Send a reply message to the vswitchd
 */
static void
send_reply_to_vswitchd(struct dpdk_message *reply)
{
	struct rte_mbuf *mbuf = NULL;
	void *ctrlmbuf_data = NULL;
	int rslt = 0;

	/* Preparing the buffer to send */
	mbuf = rte_ctrlmbuf_alloc(pktmbuf_pool);

	if (!mbuf) {
		RTE_LOG(WARNING, APP, "Error : Unable to allocate an mbuf "
		        ": %s : %d", __FUNCTION__, __LINE__);
		stats_vswitch_tx_drop_increment(INC_BY_1);
		stats_vport_rx_drop_increment(VSWITCHD, INC_BY_1);
		return;
	}

	ctrlmbuf_data = rte_ctrlmbuf_data(mbuf);
	rte_memcpy(ctrlmbuf_data, reply, sizeof(*reply));
	rte_ctrlmbuf_len(mbuf) = sizeof(*reply);

	/* Sending the buffer to vswitchd */
	rslt = rte_ring_mp_enqueue(vswitchd_reply_ring, (void *)mbuf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_ctrlmbuf_free(mbuf);
			stats_vswitch_tx_drop_increment(INC_BY_1);
			stats_vport_rx_drop_increment(VSWITCHD, INC_BY_1);
		} else {
			stats_vport_overrun_increment(VSWITCHD, INC_BY_1);
			stats_vport_rx_increment(VSWITCHD, INC_BY_1);
		}
	} else {
		stats_vport_rx_increment(VSWITCHD, INC_BY_1);
	}
}

/*
 * Send message to vswitchd indicating message type is not known
 */
static void
handle_unknown_cmd(void)
{
	struct dpdk_message reply = {0};

	reply.type = EINVAL;

	send_reply_to_vswitchd(&reply);
}

/*
 * Add or modify flow table entry.
 *
 * When modifying, the stats can be optionally cleared
 */
static void
flow_cmd_new(struct dpdk_flow_message *request)
{
	struct dpdk_message reply = {0};
	int pos = 0;
	struct action action = {0};

	pos = flow_table_lookup(&request->key);
	if (pos < 0) {
		if (request->flags & FLAG_CREATE) {
			action_output_build(&action, request->action);
			flow_table_add_flow(&request->key, &action);
			reply.type = 0;
		} else {
			reply.type = ENOENT;
		}
	} else {
		if (request->flags & FLAG_REPLACE) {
			/* Retrieve flow stats*/
			flow_table_get_flow(&request->key,
			                    NULL, &request->stats);
			/* Depending on the value of request->clear we will
			 * either update or keep the same stats
			 */
			action_output_build(&action, request->action);
			flow_table_mod_flow(&request->key,
			         &action, request->clear);
			reply.type = 0;
		} else {
			reply.type = EEXIST;
		}
	}

	reply.flow_msg = *request;

	send_reply_to_vswitchd(&reply);
}

/*
 * Delete single flow or all flows.
 *
 * When request->key is empty delete all flows
 */
static void
flow_cmd_del(struct dpdk_flow_message *request)
{
	struct dpdk_message reply = {0};
	struct flow_key empty = {0};
	int pos = 0;

	if (!memcmp(&request->key, &empty, sizeof(request->key))) {
		flow_table_del_all();
		reply.type = 0;
	} else {
		pos = flow_table_lookup(&request->key);
		if (pos < 0) {
			reply.type = ENOENT;
		} else {
			/* Retrieve flow stats*/
			flow_table_get_flow(&request->key,
			               NULL, &request->stats);
			flow_table_del_flow(&request->key);
			reply.type = 0;
		}
	}

	reply.flow_msg = *request;
	send_reply_to_vswitchd(&reply);
}

/*
 * Return flow entry to vswitchd if it exists
 */
static void
flow_cmd_get(struct dpdk_flow_message *request)
{
	struct dpdk_message reply = {0};
	int ret = 0;
	struct action action = {0};

	ret = flow_table_get_flow(&request->key, &action, &request->stats);
	if (ret < 0) {
		reply.type = ENOENT;
	} else {
		request->action = action.data.output.port;
		reply.type = 0;
	}

	reply.flow_msg = *request;
	send_reply_to_vswitchd(&reply);
}

/*
 * Dump all flows.
 *
 * The message that is received contains the key for the previously dumped
 * flow entry. If the key is zero then we are dumping the first entry. We
 * reply with EOF when we have dumped all flows
 *
 */
static void
flow_cmd_dump(struct dpdk_flow_message *request)
{
	int ret = 0;
	struct dpdk_message reply = {0};
	struct flow_key empty = {0};
	struct flow_key key = {0};
	struct flow_stats stats = {0};
	struct action action = {0};

	if (!memcmp(&request->key, &empty, sizeof(request->key))) {
		/*
		 * if key is empty, it is first call of dump(), so we
		 * need to reply using the first flow
		 */
		ret = flow_table_get_first_flow(&key, &action, &stats);
	} else {
		/* next flow */
		ret = flow_table_get_next_flow(&request->key,
		               &key, &action, &stats);
	}

	if (ret >= 0) {
		request->key = key;
		request->action = action.data.output.port;
		request->stats = stats;
		reply.type = 0;
	} else {
		/* Reached the end of the flow table */
		reply.type = EOF;
	}

	reply.flow_msg = *request;
	send_reply_to_vswitchd(&reply);
}

/*
 * Handle flow commands
 */
static void
handle_flow_cmd(struct dpdk_flow_message *request)
{
	switch (request->cmd) {
	case FLOW_CMD_NEW:
		flow_cmd_new(request);
		break;
	case FLOW_CMD_DEL:
		flow_cmd_del(request);
		break;
	case FLOW_CMD_GET:
		if (request->flags & FLAG_DUMP)
			flow_cmd_dump(request);
		else
			flow_cmd_get(request);
		break;
	default:
		handle_unknown_cmd();
	}
}

/*
 * Handle packet commands
 */
static void
handle_packet_cmd(struct dpdk_packet_message *request, struct rte_mbuf *pkt)
{
	struct action action = {0};
	action_output_build(&action, request->action);

	action_execute(&action, pkt);
}

/*
 * Parse message from vswitchd and send to appropriate handler
 */
static void
handle_vswitchd_cmd(struct rte_mbuf *mbuf)
{
	struct dpdk_message *request = NULL;

	request = rte_pktmbuf_mtod(mbuf, struct dpdk_message *);

	switch (request->type) {
	case FLOW_CMD_FAMILY:
		handle_flow_cmd(&request->flow_msg);
		rte_pktmbuf_free(mbuf);
		break;
	case PACKET_CMD_FAMILY:
		rte_pktmbuf_adj(mbuf, sizeof(*request));
		handle_packet_cmd(&request->packet_msg, mbuf);
		break;
	default:
		handle_unknown_cmd();
		rte_pktmbuf_free(mbuf);
	}
}

/*
 * Initialise the datapath and all associated data structures.
 */
void
datapath_init(void)
{
	vswitchd_packet_ring = rte_ring_create(VSWITCHD_PACKET_RING_NAME,
			         VSWITCHD_RINGSIZE, SOCKET0, NO_FLAGS);
	if (vswitchd_packet_ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create packet ring for vswitchd");

	vswitchd_reply_ring = rte_ring_create(VSWITCHD_REPLY_RING_NAME,
			         VSWITCHD_RINGSIZE, SOCKET0, NO_FLAGS);
	if (vswitchd_reply_ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create reply ring for vswitchd");

	vswitchd_message_ring = rte_ring_create(VSWITCHD_MESSAGE_RING_NAME,
			         VSWITCHD_RINGSIZE, SOCKET0, NO_FLAGS);
	if (vswitchd_message_ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create message ring for vswitchd");
}
