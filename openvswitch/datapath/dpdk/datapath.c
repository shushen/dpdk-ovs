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

#include <stdint.h>
#include <rte_config.h>
#include <rte_memcpy.h>
#include <rte_ring.h>
#include <rte_cycles.h>

#include <stdbool.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>

#include "vport.h"
#include "datapath.h"
#include "ovdk_datapath_messages.h"
#include "action.h"
#include "stats.h"
#include "init.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NO_FLAGS               0
#define SOCKET0                0
#define PKT_BURST_SIZE         32u
#define VSWITCHD_RINGSIZE      2048
#define VSWITCHD_ALLOC_THRESHOLD   (VSWITCHD_RINGSIZE/4)
#define VSWITCHD_PACKET_RING_NAME  "MProc_Vswitchd_Packet_Ring"
#define VSWITCHD_REPLY_RING_NAME   "MProc_Vswitchd_Reply_Ring"
#define VSWITCHD_MESSAGE_RING_NAME "MProc_Vswitchd_Message_Ring"
#define VSWITCHD_FREE_RING_NAME    "MProc_Vswitchd_Free_Ring"
#define VSWITCHD_ALLOC_RING_NAME   "MProc_Vswitchd_Alloc_Ring"

/* Flow messages flags bits */
#define FLAG_ROOT              0x100
#define FLAG_MATCH             0x200
#define FLAG_ATOMIC            0x400
#define FLAG_DUMP              (FLAG_ROOT | FLAG_MATCH)

#define FLAG_REPLACE           0x100
#define FLAG_EXCL              0x200
#define FLAG_CREATE            0x400
#define FLAG_APPEND            0x800

#define VPORT_CMD_FAMILY       0xE
#define FLOW_CMD_FAMILY        0xF
#define PACKET_CMD_FAMILY      0x1F

#define DPIF_SOCKNAME "\0dpif-dpdk"

/* ring to send packets to vswitchd */
static struct rte_ring *vswitchd_packet_ring = NULL;
/* ring to receive messages from vswitchd */
static struct rte_ring *vswitchd_message_ring = NULL;
/* ring to send reply messages to vswitchd */
static struct rte_ring *vswitchd_reply_ring = NULL;

/* Holds packets to be freed */
static struct rte_ring *vswitchd_free_ring = NULL;
/* Holds newly allocated packets */
static struct rte_ring *vswitchd_alloc_ring = NULL;

static void send_reply_to_vswitchd(struct dpdk_message *reply);

static void handle_vswitchd_cmd(struct rte_mbuf *mbuf);
static void handle_vport_cmd(struct dpdk_vport_message *request);
static void handle_flow_cmd(struct dpdk_flow_message *request);
static void handle_packet_cmd(struct dpdk_packet_message *request,
                              struct rte_mbuf *pkt);
static void handle_unknown_cmd(void);

static void flow_cmd_get(struct dpdk_flow_message *request);
static void flow_cmd_new(struct dpdk_flow_message *request);
static void flow_cmd_del(struct dpdk_flow_message *request);
static void flow_cmd_dump(struct dpdk_flow_message *request);

static int dpif_socket = -1;

static void send_signal_to_dpif(void)
{
	static struct sockaddr_un addr;
	int n;

	if (!addr.sun_family) {
		addr.sun_family = AF_UNIX;
		memcpy(addr.sun_path, DPIF_SOCKNAME, sizeof(DPIF_SOCKNAME));
	}

	/* don't care about error */
	sendto(dpif_socket, &n, sizeof(n), 0,
		(struct sockaddr *)&addr, sizeof(addr));
}

/*
 * Function sends unmatched packets to vswitchd.
 */
inline void __attribute__((always_inline))
send_packet_to_vswitchd(struct rte_mbuf *mbuf, struct dpdk_upcall *info)
{
	int rslt = 0;
	int cnt = 0;
	void *mbuf_ptr = NULL;

	/* send one packet, delete information about segments */
	rte_pktmbuf_pkt_len(mbuf) = rte_pktmbuf_data_len(mbuf);

	/* allocate space before the packet for the upcall info */
	mbuf_ptr = rte_pktmbuf_prepend(mbuf, sizeof(*info));

	if (unlikely(mbuf_ptr == NULL)) {
		RTE_LOG(ERR, APP, "Error : Cannot prepend upcall info "
		        ": %s : %d", __FUNCTION__, __LINE__);
		rte_pktmbuf_free(mbuf);
		stats_vswitch_tx_drop_increment(INC_BY_1);
		stats_vport_tx_drop_increment(VSWITCHD, INC_BY_1);
		return;
	}

	rte_memcpy(mbuf_ptr, info, sizeof(*info));

	cnt = rte_ring_count(vswitchd_packet_ring);

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

	/*
	 * cnt == 0 means vswitchd is in poll_block, and needed to wake up.
	 * However, current rte_ring_count == 0 means, queued packet has
	 * been processed in vswitchd, then no signaling is needed.
	 */
	if (cnt == 0 && rte_ring_count(vswitchd_packet_ring) > 0)
		send_signal_to_dpif();
}

/*
 * Function handles messages from the daemon.
 */
void
handle_request_from_vswitchd(void)
{
	uint16_t j = 0;
	uint16_t dq_pkt = 0;
	struct rte_mbuf *buf[PKT_BURST_SIZE];

	/* Attempt to dequeue maximum available number of mbufs from ring */
	dq_pkt = rte_ring_sc_dequeue_burst(vswitchd_message_ring, (void**) buf,
			PKT_BURST_SIZE);

	/* Update number of packets transmitted by daemon */
	stats_vport_rx_increment(VSWITCHD, dq_pkt);

	for (j = 0; j < dq_pkt; j++) {
		handle_vswitchd_cmd(buf[j]);
	}

	/* Free any packets from the vswitch daemon */
	dq_pkt = rte_ring_sc_dequeue_burst(vswitchd_free_ring, (void**)buf,
			PKT_BURST_SIZE);
	for (j = 0; j < dq_pkt; j++)
	    rte_pktmbuf_free(buf[j]);

	/* Allocate mbufs for the vswitch daemon to use in case the alloc ring
	 * count is too low */
	while (rte_ring_count(vswitchd_alloc_ring) < VSWITCHD_ALLOC_THRESHOLD) {
		for (j = 0; j < PKT_BURST_SIZE; j++) {
			if ((buf[j] = rte_pktmbuf_alloc(pktmbuf_pool)) == NULL)
				break;
		}
		if (j)
			rte_ring_sp_enqueue_bulk(vswitchd_alloc_ring, (void**) buf, j);
	}
}

/*
 * Send a reply message to vswitchd.
 */
static void
send_reply_to_vswitchd(struct dpdk_message *reply)
{
	struct rte_mbuf *mbuf = NULL;
	void *pktmbuf_data = NULL;
	int rslt = 0;

	/* Preparing the buffer to send */
	mbuf = rte_pktmbuf_alloc(pktmbuf_pool);

	if (!mbuf) {
		RTE_LOG(WARNING, APP, "Error : Unable to allocate an mbuf "
		        ": %s : %d", __FUNCTION__, __LINE__);
		stats_vswitch_tx_drop_increment(INC_BY_1);
		stats_vport_rx_drop_increment(VSWITCHD, INC_BY_1);
		return;
	}

	pktmbuf_data = rte_pktmbuf_mtod(mbuf, void *);
	rte_memcpy(pktmbuf_data, reply, sizeof(*reply));
	rte_pktmbuf_data_len(mbuf) = sizeof(*reply);

	/* Sending the buffer to vswitchd */
	rslt = rte_ring_mp_enqueue(vswitchd_reply_ring, (void *)mbuf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_pktmbuf_free(mbuf);
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
 * Send message to vswitchd indicating message type is not known.
 */
static void
handle_unknown_cmd(void)
{
	struct dpdk_message reply = {0};

	reply.type = EINVAL;

	send_reply_to_vswitchd(&reply);
}

/*
 * Attempt to add a new vport, and send result to vswitchd.
 *
 * This handles a request from the datapath to add a new port. The request can
 * contain a port number, a port name, or both. If a port number is not
 * available, the datapath attempts to get a free port within the port type's
 * range.
 *
 * 0 is returned to the daemon and the 'request->port_no' field updated if the
 * port number - generated or given - is available. If a port is not
 * available - because it's in use or non-existent - 'EBUSY' or 'ENODEV' is
 * returned respectively.
 */
static void
vport_cmd_new(struct dpdk_vport_message *request)
{
	struct dpdk_message reply = {0};
	uint32_t port_no;
	char *port_name;
	uint8_t port_type;

	port_type = request->type;
	port_name = request->port_name;
	port_no = request->port_no;

	/* Calculate vportid for a given Physical port index */
	if (port_type == VPORT_TYPE_PHY)
		port_no = port_no + PHYPORT0;

	/* Haven't requested a given port_number or one supplied is invalid, so get
	 * one in range */
	if (!vport_exists(port_no) || !vport_id_is_valid(port_no, port_type))
		port_no = vport_next_available_index(port_type);

	if (vport_exists(port_no)) {
		if (!vport_is_enabled(port_no)) {
			/* Haven't requested a given port_name, so use default one */
			if (port_name == NULL)
				port_name = vport_get_name(port_no);

			if (vport_attach(port_no, port_name) == 0) {
				vport_enable(port_no);

				vport_set_name(port_no, port_name);

				/* We don't need to "set type", as the port's position in the 'vport_info'
				 * array implicitly defines the type. This will change if the structure
				 * of this array changes. */

				/* Populate 'reply' */
				request->port_no = port_no;
				strncpy(request->port_name, port_name, MAX_VPORT_NAME_SIZE);
				reply.type = 0;
			} else {
				reply.type = EINVAL;
			}
		} else {
			reply.type = EBUSY;
		}
	} else {
		reply.type = ENODEV;
	}

	reply.vport_msg = *request;
	send_reply_to_vswitchd(&reply);
}

/*
 * Attempt to delete single vport, and send result to vswitchd.
 *
 * This handles a request from the datapath to delete an exists port.
 *
 * Returns 0 to the datapath is port is deleted, else 'ENODEV' if device
 * did not exist or was not enabled.
 */
static void
vport_cmd_del(struct dpdk_vport_message *request)
{
	struct dpdk_message reply = {0};
	uint16_t port_no = request->port_no;

	if (vport_exists(port_no) && vport_is_enabled(port_no)) {
		vport_disable(port_no);
		reply.type = 0;
	} else {
		reply.type = ENODEV;  /* from a high-level, the device doesn't exist */
	}

	reply.vport_msg = *request;
	send_reply_to_vswitchd(&reply);
}

/*
 * Get current stats for a single vport, and send result to vswitchd.
 */
static void
vport_cmd_get(struct dpdk_vport_message *request)
{
	struct dpdk_message reply = {0};
	uint32_t port_no = request->port_no;

	/* Correct 'port_no' value may not be included as part of 'request';
	 * identify vport number associated with vport name. This is used by
	 * the dpif's 'query_by_name' function. */
	if (port_no == UINT32_MAX)
		request->port_no = port_no = vport_name_to_portid(request->port_name);

	if (vport_exists(port_no) && vport_is_enabled(port_no)) {
		request->stats = stats_vport_get(request->port_no);
		request->type = vport_get_type(request->port_no);

		strncpy(request->port_name, vport_get_name(request->port_no),
		        MAX_VPORT_NAME_SIZE);
		reply.type = 0;
	} else {
		reply.type = ENODEV;  /* from a high-level, the device doesn't exist */
	}

	reply.vport_msg = *request;

	send_reply_to_vswitchd(&reply);
}

/*
 * Dump the next in-use vport.
 *
 * This handles the "dump ports" command, which is a request for information
 * on all existing ports in the datapath. This function is used within a
 * state machine at a higher level (i.e. vswitchd)
 */
static void
vport_cmd_dump(struct dpdk_vport_message *request)
{
	struct dpdk_message reply = {0};

	if (request->port_no == UINT32_MAX)  /* "Entry case" for state machine */
		request->port_no = 1;
	else
		request->port_no += 1;

	/* Skip all ports without the 'enabled' flag. This particular approach is
	 * necessary due to the current design of the datapath. Ports are currently
	 * declared and initialised at startup, rather than being allocated and
	 * initialised "on the fly". We need to check for 'vport_is_enabled' rather
	 * than 'vport_exists', to prevent vswitchd seeing ports that have not been
	 * explicitly added via 'ovs-vsctl'. If this were not done the vswitchd
	 * would spot the "alien" ports and try to remove them, causing issues. */
	for ( ; request->port_no < MAX_VPORTS && !vport_is_enabled(request->port_no)\
		      ; request->port_no++)
		;

	if (request->port_no < MAX_VPORTS) {
		request->stats = stats_vport_get(request->port_no);
		request->type = vport_get_type(request->port_no);
		reply.type = 0;
	} else {
		reply.type = EOF;  /* "Exit case" for state machine */
	}

	reply.vport_msg = *request;

	send_reply_to_vswitchd(&reply);
}

/*
 * Parse 'vport message' from vswitchd and send to appropriate handler.
 */
static void
handle_vport_cmd(struct dpdk_vport_message *request)
{
	switch (request->cmd) {
		case VPORT_CMD_NEW:
			vport_cmd_new(request);
			break;
		case VPORT_CMD_DEL:
			vport_cmd_del(request);
			break;
		case VPORT_CMD_GET:
			if (request->flags & FLAG_DUMP)
				vport_cmd_dump(request);
			else
				vport_cmd_get(request);
			break;
		default:
			handle_unknown_cmd();
	}
}

/*
 * Add or modify flow table entry.
 *
 * When modifying, the stats can be optionally cleared.
 */
static void
flow_cmd_new(struct dpdk_flow_message *request)
{
	struct dpdk_message reply = {0};
	int pos = 0;

	pos = flow_table_lookup(&request->key);
	if (pos < 0) {
		if (request->flags & FLAG_CREATE) {
			flow_table_add_flow(&request->key, request->actions);
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
			flow_table_mod_flow(&request->key,
			         request->actions, request->clear);
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
 * When request->key is empty delete all flows.
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
 * Return flow entry to vswitchd if it exists.
 */
static void
flow_cmd_get(struct dpdk_flow_message *request)
{
	struct dpdk_message reply = {0};
	int ret = 0;

	ret = flow_table_get_flow(&request->key, request->actions, &request->stats);
	if (ret < 0) {
		reply.type = ENOENT;
	} else {
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

	if (!memcmp(&request->key, &empty, sizeof(request->key))) {
		/*
		 * if key is empty, it is first call of dump(), so we
		 * need to reply using the first flow
		 */
		ret = flow_table_get_first_flow(&key, request->actions, &stats);
	} else {
		/* next flow */
		ret = flow_table_get_next_flow(&request->key,
		               &key, request->actions, &stats);
	}

	if (ret >= 0) {
		request->key = key;
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
 * Parse 'flow message' from vswitchd and send to appropriate handler.
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
 * Parse 'packet message' from vswitchd and send to appropriate handler.
 */
static void
handle_packet_cmd(struct dpdk_packet_message *request, struct rte_mbuf *pkt)
{
	action_execute(request->actions, pkt);
}

/*
 * Parse message from vswitchd and send to appropriate handler.
 */
static void
handle_vswitchd_cmd(struct rte_mbuf *mbuf)
{
	struct dpdk_message *request = NULL;

	request = rte_pktmbuf_mtod(mbuf, struct dpdk_message *);

	switch (request->type) {
	case VPORT_CMD_FAMILY:
		handle_vport_cmd(&request->vport_msg);
		rte_pktmbuf_free(mbuf);
		break;
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
	int one = 1;

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

	vswitchd_free_ring = rte_ring_create(VSWITCHD_FREE_RING_NAME,
	         VSWITCHD_RINGSIZE, SOCKET0, NO_FLAGS);

	if (vswitchd_free_ring == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create free ring for vswitchd");

	vswitchd_alloc_ring = rte_ring_create(VSWITCHD_ALLOC_RING_NAME,
	         VSWITCHD_RINGSIZE, SOCKET0, NO_FLAGS);

	if (vswitchd_alloc_ring == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create alloc ring for vswitchd");

	dpif_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (dpif_socket < 0)
		rte_exit(EXIT_FAILURE, "Cannot create socket");

	if (ioctl(dpif_socket, FIONBIO, &one) < 0)
		rte_exit(EXIT_FAILURE, "Cannot make socket non-blocking");
}
