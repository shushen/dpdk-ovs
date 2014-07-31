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

#include <string.h>
#include <assert.h>

#include <config.h>
#include <rte_config.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_mbuf.h>
#include <rte_string_fns.h>

#include "lib/timeval.h"

#include "vlog.h"
#include "command-line.h"
#include "dpdk-flow-stub.h"
#include "datapath/dpdk/ovdk_config.h"
#include "datapath/dpdk/ovdk_flow_types.h"
#include "datapath/dpdk/ovdk_datapath.h"
#include "datapath/dpdk/ovdk_datapath_messages.h"
#include "datapath/dpdk/ovdk_mempools.h"
#include "datapath/dpdk/ovdk_stats.h"

static struct rte_mempool *pktmbuf_pool;
static struct rte_mempool *ctrlmbuf_pool;
static struct rte_ring *request_ring;
static struct rte_ring *reply_ring;

int mempools_lookup(void);
int vswitchd_rings_lookup(unsigned lcore_id);
int enqueue_request(struct rte_mbuf *mbuf);
struct rte_ring *ring_lookup(char *template, unsigned lcore_id);
void populate_flow_msg(struct ovdk_flow_message *flow_msg, enum flow_cmd type);
void test_flow_add__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_flow_add__failure(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_flow_del__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_flow_get__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);

/* Helper Functions */

int
mempools_lookup(void)
{
	pktmbuf_pool = rte_mempool_lookup(PKTMBUF_POOL_NAME);
	ctrlmbuf_pool = rte_mempool_lookup(CTRLMBUF_POOL_NAME);

	if (!pktmbuf_pool || !ctrlmbuf_pool)
		return -1;

	return 0;
}

struct rte_ring *
ring_lookup(char *template, unsigned lcore_id)
{
	struct rte_ring *ring = NULL;
	char ring_name[OVDK_MAX_NAME_SIZE] = {0};

	snprintf(ring_name, sizeof(ring_name), template, lcore_id);
	ring = rte_ring_lookup(ring_name);

	return ring;
}

int
vswitchd_rings_lookup(unsigned lcore_id)
{
	request_ring = ring_lookup(VSWITCHD_REQUEST_RING_NAME, lcore_id);
	reply_ring   = ring_lookup(VSWITCHD_REPLY_RING_NAME, lcore_id);
	if (request_ring == NULL || reply_ring == NULL)
		return -1;

	return 0;
}

void
populate_flow_msg(struct ovdk_flow_message *flow_msg, enum flow_cmd type)
{
	memset(flow_msg, 0, sizeof(*flow_msg));

	flow_msg->cmd = type;
	flow_msg->key.in_port = 1;
	flow_msg->key.ip_src  = 0x01010101;
	flow_msg->key.ip_dst  = 0x02020202;

	flow_msg->actions[0].type = OVDK_ACTION_OUTPUT;
	flow_msg->actions[0].data.output.port = 1;
	flow_msg->num_actions = 1;
	flow_msg->flow_handle = 5;
}

int
enqueue_request(struct rte_mbuf *mbuf)
{
	if(rte_ring_sp_enqueue(request_ring, (void *)mbuf) != 0)
		return -1;

	return 0;
}


/* Test Functions */

void
test_flow_add__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct rte_mbuf *buf = NULL;
	struct ovdk_message request;
	struct ovdk_message reply;
	struct ovdk_flow_message flow_message;
	void *ctrlmbuf_data = NULL;

	/* Allocate mbuf for request */
	buf = rte_ctrlmbuf_alloc(ctrlmbuf_pool);
	assert(buf != NULL);

	/* Populate request  */
	populate_flow_msg(&flow_message, FLOW_CMD_NEW);
	request.type = OVDK_FLOW_CMD_FAMILY;
	memcpy(&request.flow_msg, &flow_message,
		sizeof(struct ovdk_flow_message));

	ctrlmbuf_data = rte_ctrlmbuf_data(buf);
	memcpy(ctrlmbuf_data, &request, sizeof(request));
	rte_ctrlmbuf_len(buf) = sizeof(request);

	/* Enqueue mbuf to request ring */
	assert(enqueue_request(buf) == 0);

	assert(rte_ring_count(reply_ring) ==  0);
	ovdk_datapath_handle_vswitchd_cmd();
	assert(rte_ring_count(reply_ring) == 1);

	/* Check that flow_message in mbuf enqueued on reply ring
	 * is the same as that enqueued on request ring (with the exception
	 * that the flow_handle differs).
	 */
	buf = NULL;
	assert(rte_ring_sc_dequeue(reply_ring, (void **)&buf) == 0);

	ctrlmbuf_data = rte_ctrlmbuf_data(buf);
	memcpy(&reply, ctrlmbuf_data, sizeof(struct ovdk_message));

	assert(reply.error == 0);
	assert(reply.type == OVDK_FLOW_CMD_FAMILY);
	assert(reply.flow_msg.cmd == request.flow_msg.cmd);
	assert(reply.flow_msg.key.in_port == request.flow_msg.key.in_port);
	assert(reply.flow_msg.key.ip_src == request.flow_msg.key.ip_src);
	assert(reply.flow_msg.key.ip_dst == request.flow_msg.key.ip_dst);
	assert(reply.flow_msg.num_actions == request.flow_msg.num_actions);
	assert(memcmp(&(reply.flow_msg.actions), &(request.flow_msg.actions),
		     (sizeof(struct ovdk_action) * OVDK_MAX_ACTIONS)) == 0);
}

void
test_flow_add__failure(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct rte_mbuf *buf = NULL;
	struct ovdk_message request;
	struct ovdk_message reply;
	struct ovdk_flow_message flow_message;
	void *ctrlmbuf_data = NULL;

	/* Allocate mbuf for request */
	buf = rte_ctrlmbuf_alloc(ctrlmbuf_pool);
	assert(buf != NULL);

	/* Populate request */
	populate_flow_msg(&flow_message, FLOW_CMD_NEW);
	flow_message.actions[0].data.output.port = OVDK_MAX_VPORTS;
	request.type = OVDK_FLOW_CMD_FAMILY;
	memcpy(&request.flow_msg, &flow_message,
		sizeof(struct ovdk_flow_message));

	ctrlmbuf_data = rte_ctrlmbuf_data(buf);
	memcpy(ctrlmbuf_data, &request, sizeof(request));
	rte_ctrlmbuf_len(buf) = sizeof(request);

	/* Enqueue mbuf to request ring */
	assert(enqueue_request(buf) == 0);

	assert(rte_ring_count(reply_ring) ==  0);
	ovdk_datapath_handle_vswitchd_cmd();
	assert(rte_ring_count(reply_ring) == 1);

	/* Check that flow_message in mbuf enqueued on reply ring
	 * is the same as that enqueued on request ring (with the exception
	 * that the flow_handle differs).
	 */
	buf = NULL;
	assert(rte_ring_sc_dequeue(reply_ring, (void **)&buf) == 0);

	ctrlmbuf_data = rte_ctrlmbuf_data(buf);
	memcpy(&reply, ctrlmbuf_data, sizeof(struct ovdk_message));

	assert(reply.type == OVDK_FLOW_CMD_FAMILY);
	assert(reply.error == -1);
}

void
test_flow_del__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct rte_mbuf *buf = NULL;
	struct ovdk_message request;
	struct ovdk_message reply;
	struct ovdk_flow_message flow_message;
	void *ctrlmbuf_data = NULL;

	/* Allocate mbuf for request */
	buf = rte_ctrlmbuf_alloc(ctrlmbuf_pool);
	assert(buf != NULL);

	/* Populate request  */
	populate_flow_msg(&flow_message, FLOW_CMD_DEL);
	request.type = OVDK_FLOW_CMD_FAMILY;
	memcpy(&request.flow_msg, &flow_message,
		sizeof(struct ovdk_flow_message));

	ctrlmbuf_data = rte_ctrlmbuf_data(buf);
	memcpy(ctrlmbuf_data, &request, sizeof(request));
	rte_ctrlmbuf_len(buf) = sizeof(request);

	/* Enqueue mbuf to request ring */
	assert(enqueue_request(buf) == 0);

	/* ovdk_datapath_flow_del is called from within ovdk_datapath_handle_vswitchd_cmd */
	assert(rte_ring_count(reply_ring) ==  0);
	ovdk_datapath_handle_vswitchd_cmd();
	assert(rte_ring_count(reply_ring) == 1);

	/* Check that flow_message in mbuf enqueued on reply ring
	 * is the same as that enqueued on request ring (with the exception
	 * that the flow_handle differs).
	 */
	buf = NULL;
	assert(rte_ring_sc_dequeue(reply_ring, (void **)&buf) == 0);

	ctrlmbuf_data = rte_ctrlmbuf_data(buf);
	memcpy(&reply, ctrlmbuf_data, sizeof(reply));

	assert(reply.type == OVDK_FLOW_CMD_FAMILY);
	assert(reply.error == 0);
	assert(reply.flow_msg.cmd == request.flow_msg.cmd);
	assert(reply.flow_msg.key.in_port == request.flow_msg.key.in_port);
	assert(reply.flow_msg.key.ip_src == request.flow_msg.key.ip_src);
	assert(reply.flow_msg.key.ip_dst == request.flow_msg.key.ip_dst);
	assert(reply.flow_msg.num_actions == request.flow_msg.num_actions);
	assert(memcmp(&(reply.flow_msg.actions), &(request.flow_msg.actions),
		     (sizeof(struct ovdk_action) * OVDK_MAX_ACTIONS)) == 0);
	assert(reply.flow_msg.stats.packet_count == DPDK_FLOW_STUB_STATS_PACKET_COUNT);
	assert(reply.flow_msg.stats.byte_count   == DPDK_FLOW_STUB_STATS_BYTE_COUNT);
	/* Stats parameter depends on current time so it cannot be tested precisely */
	assert(reply.flow_msg.stats.used         >  DPDK_FLOW_STUB_STATS_USED);
	assert(reply.flow_msg.stats.tcp_flags    == DPDK_FLOW_STUB_STATS_TCP_FLAGS);
}

void
test_flow_get__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED) {
	struct rte_mbuf *buf = NULL;
	struct ovdk_message request;
	struct ovdk_message reply;
	struct ovdk_flow_message flow_message;
	void *ctrlmbuf_data = NULL;

	/* Allocate mbuf for request */
	buf = rte_ctrlmbuf_alloc(ctrlmbuf_pool);
	assert(buf != NULL);

	/* Populate request  */
	flow_message.cmd = FLOW_CMD_GET;
	flow_message.flow_handle = 1;
	request.type = OVDK_FLOW_CMD_FAMILY;
	memcpy(&request.flow_msg, &flow_message,
		sizeof(struct ovdk_flow_message));

	ctrlmbuf_data = rte_ctrlmbuf_data(buf);
	memcpy(ctrlmbuf_data, &request, sizeof(request));
	rte_ctrlmbuf_len(buf) = sizeof(request);

	/* Enqueue mbuf to request ring */
	assert(enqueue_request(buf) == 0);

	/* ovdk_datapath_flow_get is called from within
	 * ovdk_datapath_handle_vswitchd_cmd
	 * */
	assert(rte_ring_count(reply_ring) ==  0);
	ovdk_datapath_handle_vswitchd_cmd();
	assert(rte_ring_count(reply_ring) == 1);

	/* Check that flow_message in mbuf enqueued on reply ring
	 * is modified as expected by the datapath get() function
	 */
	buf = NULL;
	assert(rte_ring_sc_dequeue(reply_ring, (void **)&buf) == 0);

	ctrlmbuf_data = rte_ctrlmbuf_data(buf);
	memcpy(&reply, ctrlmbuf_data, sizeof(reply));

	assert(reply.error == 0);
	assert(reply.type == OVDK_FLOW_CMD_FAMILY);
	assert(reply.flow_msg.cmd == request.flow_msg.cmd);
	assert(reply.flow_msg.stats.packet_count == DPDK_FLOW_STUB_STATS_PACKET_COUNT);
	assert(reply.flow_msg.stats.byte_count   == DPDK_FLOW_STUB_STATS_BYTE_COUNT);
	assert(reply.flow_msg.stats.used         == DPDK_FLOW_STUB_STATS_USED);
	assert(reply.flow_msg.stats.tcp_flags    == DPDK_FLOW_STUB_STATS_TCP_FLAGS);
	assert(reply.flow_msg.actions[0].type    == 0);
	assert(reply.flow_msg.actions[1].type    == 0);
	assert(reply.flow_msg.num_actions        == 2);
}

static const struct command commands[] = {
	{"flow-add-default", 0, 0, test_flow_add__default},
	{"flow-add-failure", 0, 0, test_flow_add__failure},
	{"flow-del-default", 0, 0, test_flow_del__default},
	{"flow-get-default", 0, 0, test_flow_get__default},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	unsigned lcore_id = rte_lcore_id();
	int eal_args = 0;

	set_program_name(argv[0]);
	vlog_set_levels(NULL, VLF_ANY_FACILITY, VLL_EMER);
	vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

	time_alarm(30);  /* Allow a long time for DPDK to start */

	/* Initialise system */
	eal_args = rte_eal_init(argc, argv);
	assert(eal_args > 0);
	eal_args += 1;  /* we *always* pass in '--' as a parameter */

	/* Initialise mempools and datapath */
	ovdk_mempools_init();
	ovdk_datapath_init();
	ovdk_stats_init();
	assert(mempools_lookup() == 0);
	assert(vswitchd_rings_lookup(lcore_id) == 0);

	run_command(argc - eal_args, argv + eal_args, commands);

	return 0;
}

