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

/*
 * ovdk_datapath
 *
 * ovdk_datapath handles messages received from the vswitchd. Each  message is
 * contained in an mbuf.
 *
 * Communication between the vswitchd and ovdk_datapath is achieved through
 * four rings:
 *
 *  - request ring: Messages sent from the vswitchd to the datapath
 *  - reply ring:   Messages sent from the datapath to the vswitchd
 *  - free ring:    Mbufs sent by the vswitchd to be freed by the datapath
 *  - alloc ring:   Mbufs allocated by the datapath to be used by the vswitchd
 *
 * ovdk_datapath has the responsibility of parsing each message received from
 * the request ring. When the message is parsed, ovdk_datapath will call the
 * relevant functions in ovdk_pipeline in order to configure the ovdk pipeline
 * in response to the message received.
 *
 * ovdk_datapath also has the responsibility of alloc'ing new mbufs from the
 * pool and sending those mbufs to the vswitchd. The vswitchd can then dequeue
 * these mbufs when it needs mbufs to send to the datapath. ovdk_datapath has
 * the responsibility of free'ing mbufs received from the free ring. This is
 * required as the vswitchd does not use DPDK threads. When using mbuf free
 * and alloc functions in a standard pthread, memory corruption can occur as
 * pthreads may be sharing mbuf caches.
 *
 * Users of ovdk_datapath must call ovdk_datapath_handle_vswitchd_cmd()
 * periodically in order to handle messages from vswitchd.
 */

#include <config.h>

#include <rte_config.h>
#include <rte_string_fns.h>
#include <rte_cycles.h>
#include <linux/netlink.h>

#include "ovdk_datapath.h"
#include "ovdk_mempools.h"
#include "ovdk_datapath_messages.h"
#include "ovdk_pipeline.h"
#include "ovdk_stats.h"
#include "ovdk_vport.h"

#define VSWITCHD_RINGSIZE           2048
#define VSWITCHD_ALLOC_THRESHOLD    (VSWITCHD_RINGSIZE/4)

#define VSWITCHD_PKT_BURST_SIZE     32

#define NO_FLAGS                    0
#define RTE_LOGTYPE_APP             RTE_LOGTYPE_USER1

/*
 * Each lcore has an ovdk_datapath to manage messages sent from the vswitchd
 * to the dataplane. This communication is done via struct rte_ring structures.
 * 'struct ovdk_datapath' maintains references to these rings.
 *
 * 'request' and 'reply' rings are for control messages. 'alloc' and 'free' are
 * available to the vswitchd to alloc and free mbufs as it cannot directly call
 * 'rte_X_alloc()' functions in a pthread without potentially corrupting the
 * mbuf caches.
 */
struct ovdk_datapath {
	struct rte_ring *request_ring;
	struct rte_ring *reply_ring;
	struct rte_ring *control_free_ring;
	struct rte_ring *control_alloc_ring;
} __rte_cache_aligned;

/* mempool for control messages */
static struct rte_mempool *ctrlmbuf_pool = NULL;

/* each lcore has a set of rings to communicate with the vswitchd */
static struct ovdk_datapath ovdk_datapath[RTE_MAX_LCORE];

static void handle_vswitchd_cmd(struct rte_mbuf *mbuf);

/* handle vport messages */
static void handle_vport_cmd(struct ovdk_vport_message *request);
static int ovdk_datapath_vport_new(struct ovdk_vport_message *request);
static int ovdk_datapath_vport_del(struct ovdk_vport_message *request);
static int ovdk_datapath_vport_get(struct ovdk_vport_message *request);

/* handle flow messages */
static void handle_flow_cmd(struct ovdk_flow_message *request);
static int ovdk_datapath_flow_new(struct ovdk_flow_message *request);
static int ovdk_datapath_flow_del(struct ovdk_flow_message *request);
static void ovdk_datapath_flow_get(struct ovdk_flow_message *request);

static void handle_unknown_cmd(void);

/* Send reply message from the datapath to the vswitchd */
static void ovdk_datapath_send_reply(struct ovdk_message *reply);

static struct rte_ring *ring_create(const char *template);
static void alloc_mbufs(struct rte_ring *alloc_ring);
static void free_mbufs(struct rte_ring *free_ring);

/* Create an rte_ring using a known template and the lcore id */
static struct rte_ring *
ring_create(const char *template)
{
	struct rte_ring *ring = NULL;
	char ring_name[OVDK_MAX_NAME_SIZE] = {0};
	unsigned lcore_id = rte_lcore_id();
	unsigned socket_id = rte_socket_id();

	snprintf(ring_name, sizeof(ring_name), template, lcore_id);
	ring = rte_ring_create(ring_name, VSWITCHD_RINGSIZE, socket_id,
	                       NO_FLAGS);
	if (ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create ring %s", ring_name);

	RTE_LOG(INFO, APP, "Created %s\n", ring_name);
	return ring;
}

/*
 * Dequeue control  mbufs from 'free-ring' and free.
 */
static void
free_mbufs(struct rte_ring *free_ring)
{
	uint16_t j = 0, dq_pkt = 0;
	struct rte_mbuf *buf[VSWITCHD_PKT_BURST_SIZE];

	dq_pkt = rte_ring_sc_dequeue_burst(free_ring, (void**)buf,
		VSWITCHD_PKT_BURST_SIZE);

	for (j = 0; j < dq_pkt; j++)
		rte_ctrlmbuf_free(buf[j]);
}

/*
 * Allocate control mbufs and enqueue onto 'alloc_ring'
 */
static void
alloc_mbufs(struct rte_ring *alloc_ring)
{
	uint16_t i = 0;
	struct rte_mbuf *buf[VSWITCHD_PKT_BURST_SIZE];

	while (rte_ring_count(alloc_ring) < VSWITCHD_ALLOC_THRESHOLD) {
		for (i = 0; i < VSWITCHD_PKT_BURST_SIZE; i++) {
			if ((buf[i] = rte_ctrlmbuf_alloc(ctrlmbuf_pool))
				    == NULL)
				break;
		}
		if (i)
			rte_ring_sp_enqueue_bulk(alloc_ring,
						 (void**) buf, i);
	}
}

int
ovdk_datapath_init(void)
{
	unsigned lcore_id = 0;

	lcore_id = rte_lcore_id();

	RTE_LOG(INFO, APP, "Creating datapath rings\n");
	ovdk_datapath[lcore_id].reply_ring = ring_create(
	                VSWITCHD_REPLY_RING_NAME);
	ovdk_datapath[lcore_id].request_ring = ring_create(
	                VSWITCHD_REQUEST_RING_NAME);
	ovdk_datapath[lcore_id].control_free_ring = ring_create(
	                VSWITCHD_CONTROL_FREE_RING_NAME);
	ovdk_datapath[lcore_id].control_alloc_ring = ring_create(
	                VSWITCHD_CONTROL_ALLOC_RING_NAME);

	ctrlmbuf_pool = rte_mempool_lookup(CTRLMBUF_POOL_NAME);
	if (ctrlmbuf_pool == NULL)
		rte_panic("Unable to lookup ctrlmbuf pool %s\n",
		          CTRLMBUF_POOL_NAME);

	return 0;
}

/*
 * Handles the VPORT_NEW message from the vswitchd by setting up ovdk_pipeline
 * correctly.
 */
static int
ovdk_datapath_vport_new(struct ovdk_vport_message *request)
{
	struct ovdk_message reply = {0};
	uint32_t vportid = 0;
	uint32_t flags = 0;
	char *vport_name = NULL;
	int ret = 0;

	RTE_LOG(DEBUG, APP, "%s(%d): %p\n",__func__,__LINE__, request);

	reply.type = OVDK_VPORT_CMD_FAMILY;

	vportid = request->vportid;
	flags = request->flags;
	vport_name = request->port_name;
	reply.vport_msg = *request;

	ret = ovdk_vport_port_verify(vportid);
	if (ret != 0) {
		RTE_LOG(WARNING, APP, "Invalid port ID for new port '%u', "
		        "error '%d'\n", vportid, ret);
		reply.error = ret;
		ovdk_datapath_send_reply(&reply);

		return ret;
	}

	if (flags & VPORT_FLAG_IN_PORT) {
		ret = ovdk_pipeline_port_in_add(vportid, vport_name);
		if (ret) {
			RTE_LOG(WARNING, APP, "Unable to add in-port '%"PRIu32"', "
			        "error '%d'\n", vportid, ret);
			reply.error = ret;
			ovdk_datapath_send_reply(&reply);
			return ret;
		}
		RTE_LOG(DEBUG, APP, "%s(%d): Added vport id '%u', '%s' "
		        "as in-port on lcore_id '%d'\n",
		        __func__,__LINE__,vportid, vport_name, rte_lcore_id());
	}

	if (flags & VPORT_FLAG_OUT_PORT) {
		ret = ovdk_pipeline_port_out_add(vportid);
		if (ret) {
			RTE_LOG(WARNING, APP, "Unable to add out-port '%"PRIu32"', "
			        "error '%d'\n", vportid, ret);
			reply.error = ret;
			ovdk_datapath_send_reply(&reply);
			return ret;
		}
		RTE_LOG(DEBUG, APP, "%s(%d): Added vport id '%u', '%s' "
		        "as out-port on lcore_id '%d'\n",
		        __func__,__LINE__,vportid, vport_name, rte_lcore_id());
	}

	reply.error = 0;
	ovdk_datapath_send_reply(&reply);

	return 0;
}

/*
 * Handles the VPORT_DEL message from the vswitchd by setting up ovdk_pipeline
 * correctly.
 */
static int
ovdk_datapath_vport_del(struct ovdk_vport_message *request)
{
	struct ovdk_message reply = {0};
	uint32_t vportid = 0;
	uint32_t flags = 0;
	int ret = 0;

	RTE_LOG(DEBUG, APP, "%s(%d): %p\n",__func__,__LINE__, request);

	reply.type = OVDK_VPORT_CMD_FAMILY;

	vportid = request->vportid;
	flags = request->flags;
	reply.vport_msg = *request;

	if (flags & VPORT_FLAG_IN_PORT) {
		ret = ovdk_pipeline_port_in_del(vportid);
		if (ret) {
			RTE_LOG(WARNING, APP, "Unable to delete in-port '%"PRIu32"', "
			        "error '%d'\n", vportid, ret);
			reply.error = ret;
			ovdk_datapath_send_reply(&reply);
			return ret;
		}
		RTE_LOG(DEBUG, APP, "Deleted vport id '%u', '%s' "
		        "as in-port on lcore_id '%d'\n",
		        vportid, request->port_name, rte_lcore_id());
	}

	if (flags & VPORT_FLAG_OUT_PORT) {
		ret = ovdk_pipeline_port_out_del(vportid);
		if (ret) {
			RTE_LOG(WARNING, APP, "Unable to delete out-port '%"PRIu32"', "
			        "error '%d'\n", vportid, ret);
			reply.error = ret;
			ovdk_datapath_send_reply(&reply);
			return ret;
		}
		RTE_LOG(DEBUG, APP, "Deleted vport id '%u', '%s' "
		        "as out-port on lcore_id '%d'\n",
		        vportid, request->port_name, rte_lcore_id());
	}

	reply.error = 0;
	ovdk_datapath_send_reply(&reply);

	return 0;
}

/*
 * Handles the VPORT_GET message from the vswitchd by setting up ovdk_pipeline
 * correctly.
 */
static int
ovdk_datapath_vport_get(struct ovdk_vport_message *request)
{
	struct ovdk_message reply = {0};
	uint32_t vportid = 0;
	struct ovdk_port_stats *stats = NULL;
	int ret = 0;

	RTE_LOG(DEBUG, APP, "%s(%d): %p\n",__func__,__LINE__, request);

	reply.type = OVDK_VPORT_CMD_FAMILY;

	vportid = request->vportid;

	reply.vport_msg = *request;
	stats = &reply.vport_msg.stats;

	ret = ovdk_stats_vport_get(vportid, stats);

	reply.error = ret;
	ovdk_datapath_send_reply(&reply);

	return 0;
}

static int
ovdk_datapath_flow_mod_del(struct ovdk_flow_message *request,
                           struct ovdk_message *reply,
                           struct ovdk_flow_stats *local_stats,
                           int *update_stats)
{
	int ret = 0;
	int del_key_found = 0;

	/* Try and delete the existing flow. */
	ret = ovdk_pipeline_flow_del(&request->key,
                                  &del_key_found,
                                  &(reply->flow_msg.stats));
	if (ret) {
		RTE_LOG(WARNING, APP, "Unable to delete flow, error '%d'\n", ret);
		return ret;
	}
	/*
	 * At this point the existing flow does not existing in the pipeline,
	 * however we need to find out if it has been found and deleted as
	 * part of the above ovdk_pipeline_flow_del() call, as we may not have
	 * permission to create a new flow.
	 */
	if (del_key_found == 0) {
		/*
		 * Key not found during attempted delete - the flow does not exist
		 * but has not been deleted as part of the above
		 * ovdk_pipeline_flow_del() call
		 */
		if ((request->flags & NLM_F_CREATE) == 0) {
			/*
			 * The flag indicates that we should not create the flow if
			 * the one it is intended to replace is not found
			 */
			RTE_LOG(WARNING, APP, "Unable to replace flow\n");
			return ENOENT;
		}
	} else {
		*local_stats = reply->flow_msg.stats;
		/*
		 * The key was found and the flow was deleted as part of the above
		 * ovdk_pipeline_flow_del() call
		 */
		/* Need to put stats from del flow into new flow */
		if (!request->clear) {
			*update_stats = 1;

		}
	}

	return ret;
}

/*
 * Handles the FLOW_NEW message from the vswitchd by setting up ovdk_pipeline
 * correctly.
 */
static int
ovdk_datapath_flow_new(struct ovdk_flow_message *request)
{
	struct ovdk_message reply = {0};
	uint64_t flow_handle = 0;
	int ret = 0;
	struct ovdk_flow_stats local_stats = {0};
	int update_stats = 0;

	RTE_LOG(DEBUG, APP, "%s(%d): %p\n",__func__,__LINE__, request);

	reply.type = OVDK_FLOW_CMD_FAMILY;

	reply.flow_msg = *request;

	if (request->flags & NLM_F_REPLACE)
	{
		/* Try to remove existing flow with same key*/
		ret = ovdk_datapath_flow_mod_del(request, &reply,
                                         &local_stats, &update_stats);
		if (ret) {
			RTE_LOG(WARNING, APP, "Unable to modify existing flow, "
			        "error '%d'\n", ret);
			reply.error = ret;
			ovdk_datapath_send_reply(&reply);
			return ret;
		}
	}

	ret = ovdk_pipeline_flow_add(&request->key, &request->actions[0],
	                             request->num_actions, &flow_handle);
	if (ret) {
		RTE_LOG(WARNING, APP, "Unable to add flow, error '%d'\n", ret);
		reply.error = ret;
		ovdk_datapath_send_reply(&reply);
		return ret;
	}

	if (update_stats && flow_handle) {
		/* Add the flow's used stats to the flow table in raw format (cycles),
		 * but convert this value to seconds in the reply message
		 */
		ovdk_pipeline_flow_set_stats(&local_stats, flow_handle);
		if (reply.flow_msg.stats.used != 0)
			reply.flow_msg.stats.used = ovs_flow_used_time(rte_rdtsc(),
		                                             reply.flow_msg.stats.used);
	}
	if (update_stats == 0) {
		/* Stats for a new flow are zero */
		reply.flow_msg.stats.used = 0;
		reply.flow_msg.stats.byte_count = 0;
		reply.flow_msg.stats.packet_count = 0;
		reply.flow_msg.stats.tcp_flags = 0;
	}

	if (request->clear) {
		/* Return stats for modified flow in reply message */
		reply.flow_msg.stats = local_stats;
		if (reply.flow_msg.stats.used != 0)
			reply.flow_msg.stats.used = ovs_flow_used_time(rte_rdtsc(),
                                                     reply.flow_msg.stats.used);
	}

	RTE_LOG(DEBUG, APP, "Added flow, flow handle '0x%lX'\n", flow_handle);

	reply.error = 0;
	reply.flow_msg.flow_handle = flow_handle;
	ovdk_datapath_send_reply(&reply);

	return 0;
}

/*
 * Handles the FLOW_DEL message from the vswitchd by setting up ovdk_pipeline
 * correctly.
 */
static int
ovdk_datapath_flow_del(struct ovdk_flow_message *request)
{
	struct ovdk_message reply = {0};
	int ret = 0;

	RTE_LOG(DEBUG, APP, "%s(%d): %p\n",__func__,__LINE__, request);

	reply.type = OVDK_FLOW_CMD_FAMILY;

	reply.flow_msg = *request;

	ret = ovdk_pipeline_flow_del(&request->key, NULL, &(reply.flow_msg.stats));
	if (ret) {
		RTE_LOG(WARNING, APP, "Unable to delete flow, error '%d'\n", ret);
		reply.error = ret;
		ovdk_datapath_send_reply(&reply);
		return ret;
	}

	RTE_LOG(DEBUG, APP, "Deleted flow\n");

	/* Convert the flow's used stats from cycles to seconds in the reply
	 * message
	 */
	reply.flow_msg.stats.used = ovs_flow_used_time(rte_rdtsc(),
	                                      reply.flow_msg.stats.used);

	reply.error = 0;
	ovdk_datapath_send_reply(&reply);

	return 0;
}

/*
 * Handles the FLOW_GET message from the vswitchd by setting up ovdk_pipeline
 * correctly.
 */
static void
ovdk_datapath_flow_get(struct ovdk_flow_message *request)
{
	struct ovdk_message reply = {0};

	RTE_LOG(DEBUG, APP, "%s(%d): %p\n",__func__,__LINE__, request);

	reply.flow_msg = *request;
	reply.type = OVDK_FLOW_CMD_FAMILY;

	if ((struct ovdk_pipeline_entry *) request->flow_handle == NULL) {
		reply.error = EINVAL;
		ovdk_datapath_send_reply(&reply);
		return;
	}

	ovdk_pipeline_flow_get_stats(&(reply.flow_msg.stats),
	                             request->flow_handle);

	ovdk_pipeline_flow_get_actions(reply.flow_msg.actions,
	                               &(reply.flow_msg.num_actions),
	                               request->flow_handle);

	reply.error = 0;
	ovdk_datapath_send_reply(&reply);
}

/*
 * Send 'reply' message to vswitchd.
 */
static void
ovdk_datapath_send_reply(struct ovdk_message *reply)
{
	struct rte_mbuf *mbuf = NULL;
	void *ctrlmbuf_data = NULL;
	int rslt = 0;
	unsigned lcore_id = 0;
	struct rte_ring *vswitchd_reply_ring = NULL;

	lcore_id = rte_lcore_id();

	vswitchd_reply_ring = ovdk_datapath[lcore_id].reply_ring;

	/* Preparing the buffer to send */
	mbuf = rte_ctrlmbuf_alloc(ctrlmbuf_pool);
	if (!mbuf) {
		RTE_LOG(WARNING, APP, "Unable to allocate an mbuf\n");
		ovdk_stats_vswitch_control_tx_drop_increment(1);
		return;
	}

	ctrlmbuf_data = rte_ctrlmbuf_data(mbuf);
	rte_memcpy(ctrlmbuf_data, reply, sizeof(struct ovdk_message));
	rte_ctrlmbuf_len(mbuf) = sizeof(struct ovdk_message);

	/* Sending the buffer to vswitchd */
	rslt = rte_ring_mp_enqueue(vswitchd_reply_ring, (void *)mbuf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_ctrlmbuf_free(mbuf);
			ovdk_stats_vswitch_control_tx_drop_increment(1);
		} else {
			ovdk_stats_vswitch_control_tx_increment(1);
			ovdk_stats_vswitch_control_overrun_increment(1);
		}
	} else {
		ovdk_stats_vswitch_control_tx_increment(1);
	}
}


/*
 * ovdk_datapath_handle_vswitchd_cmd() receives messages from the vswitchd. It
 * must be run periodically on each core that runs an ovdk_pipeline. It only
 * handles messages for the ovdk_pipeline on that core.
 *
 * ovdk_datapath_handle_vswitchd_cmd() frees and allocs mbufs from the free and
 * alloc rings respectively.
 */
void
ovdk_datapath_handle_vswitchd_cmd(void)
{
	uint16_t j = 0, dq_pkt = 0;
	struct rte_mbuf *buf[VSWITCHD_PKT_BURST_SIZE];
	unsigned lcore_id = 0;
	struct rte_ring *vswitchd_control_alloc_ring = NULL;
	struct rte_ring *vswitchd_control_free_ring = NULL;
	struct rte_ring *vswitchd_message_ring = NULL;

	lcore_id = rte_lcore_id();

	vswitchd_control_alloc_ring = ovdk_datapath[lcore_id].control_alloc_ring;
	vswitchd_control_free_ring = ovdk_datapath[lcore_id].control_free_ring;
	vswitchd_message_ring = ovdk_datapath[lcore_id].request_ring;

	/* Attempt to dequeue maximum available number of mbufs from ring */
	dq_pkt = rte_ring_sc_dequeue_burst(vswitchd_message_ring, (void**) buf,
	                                   VSWITCHD_PKT_BURST_SIZE);

	/* Update number of packets transmitted by daemon */
	ovdk_stats_vswitch_control_rx_increment(dq_pkt);

	for (j = 0; j < dq_pkt; j++) {
		handle_vswitchd_cmd(buf[j]);
	}

	/* Free any packets from the vswitch daemon */
	free_mbufs(vswitchd_control_free_ring);

	/* Allocate mbufs for the vswitch daemon to use in case the alloc ring
	 * count is too low */
	alloc_mbufs(vswitchd_control_alloc_ring);
}

/*
 * Send message to vswitchd indicating message type is not known.
 */
static void
handle_unknown_cmd(void)
{
	struct ovdk_message reply = {0};

	reply.error = EINVAL;

	ovdk_datapath_send_reply(&reply);
}

/*
 * Parse 'flow message' from vswitchd and send to appropriate handler.
 */
static void
handle_flow_cmd(struct ovdk_flow_message *request)
{
	switch (request->cmd) {
	case FLOW_CMD_NEW:
		ovdk_datapath_flow_new(request);
		break;
	case FLOW_CMD_DEL:
		ovdk_datapath_flow_del(request);
		break;
	case FLOW_CMD_GET:
		ovdk_datapath_flow_get(request);
		break;
	default:
		handle_unknown_cmd();
	}
}

/*
 * Parse 'vport message' from vswitchd and send to appropriate handler.
 */
static void
handle_vport_cmd(struct ovdk_vport_message *request)
{
	switch (request->cmd) {
	case VPORT_CMD_NEW:
		ovdk_datapath_vport_new(request);
		break;
	case VPORT_CMD_DEL:
		ovdk_datapath_vport_del(request);
		break;
	case VPORT_CMD_GET:
		ovdk_datapath_vport_get(request);
		break;
	default:
		handle_unknown_cmd();
	}
}

/*
 * Parse message from vswitchd and send to appropriate handler.
 */
static void
handle_vswitchd_cmd(struct rte_mbuf *mbuf)
{
	struct ovdk_message *request = NULL;

	request = rte_ctrlmbuf_data(mbuf);

	switch (request->type) {
	case OVDK_VPORT_CMD_FAMILY:
		handle_vport_cmd(&request->vport_msg);
		rte_ctrlmbuf_free(mbuf);
		break;
	case OVDK_FLOW_CMD_FAMILY:
		handle_flow_cmd(&request->flow_msg);
		rte_ctrlmbuf_free(mbuf);
		break;
	default:
		handle_unknown_cmd();
		rte_ctrlmbuf_free(mbuf);
	}
}
