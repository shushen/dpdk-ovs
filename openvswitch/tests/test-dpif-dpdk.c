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

#include <config.h>
#include <string.h>
#include <assert.h>
#include <netinet/in.h>

#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_memzone.h>
#include <rte_ether.h>
#include <rte_byteorder.h>

#include "config.h"
#include "command-line.h"
#include "timeval.h"
#include "util.h"
#include "vlog.h"
#include "datapath/dpdk/ovdk_datapath_messages.h"
#include "datapath/dpdk/ovdk_action_types.h"
#include "dpif-dpdk-flow-table.h"
#include "dpif-dpdk-vport-table.h"
#include "datapath/dpdk/ovdk_vport_types.h"
#include "dpif.h"
#include "dpif-provider.h"
#include "dpif-dpdk.h"
#include "dpdk-link.h"
#include "netdev-provider.h"
#include "netdev-dpdk.h"
#include "netlink.h"
#include "ovs-thread.h"
#include "dpdk-ring-stub.h"
#include "dpdk-flow-stub.h"
#include "odp-util.h"

#define MAX_OPS        10
#define MAX_ACTIONS    32

static struct dpif dpif;
static struct dpif *dpif_p = &dpif;

/* Bitmask of enabled cores determined by call to dpdk_link_init */
uint64_t pipeline_mask = 0x2; /* Only use pipeline 1 for testing */

struct dpif_dpdk_flow_state {
    struct ovdk_flow_message flow;
    struct dpif_flow_stats stats;
    struct ofpbuf actions_buf;
    struct ofpbuf key_buf;
    uint16_t flow_table_index;
};

void test_dpif_dpdk_get_stats(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_add(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_del(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_query_by_number(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_query_by_name(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_get_max_ports(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_dump_start(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_dump_next(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_dump_done(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_poll(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_poll_wait(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_port_get_stats(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);

void test_dpif_dpdk_flow_put(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_del(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_del__null_key(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_del__flow_entry_not_found(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_del__null_dpif_flow_del(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_get(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_get__null_key(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_get__flow_entry_not_found(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_dump_start(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_dump_next(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_dump_next__null_state(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_dump_next__no_entry(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_dump_done(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_dump_done__null_state(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_flow_flush(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);

void test_dpif_dpdk_recv(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_operate(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_dpif_dpdk_execute(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void populate_flow(struct flow *flow);

void populate_flow(struct flow *flow)
{
	memset(flow, 0, sizeof(struct flow));
	flow->in_port.odp_port = DPDK_RING_STUB_IN_PORT;
	flow->nw_proto = DPDK_RING_STUB_NW_PROTO;
	flow->tp_src = rte_cpu_to_be_16(DPDK_RING_STUB_TP_SRC);
	flow->tp_dst = rte_cpu_to_be_16(DPDK_RING_STUB_TP_DST);
	flow->nw_src = rte_cpu_to_be_32(DPDK_RING_STUB_NW_SRC);
	flow->nw_dst = rte_cpu_to_be_32(DPDK_RING_STUB_NW_DST);
	flow->vlan_tci = rte_cpu_to_be_16(DPDK_RING_STUB_VLAN_TCI);
	flow->dl_dst[0] = DPDK_RING_STUB_DL_DST_0;
	flow->dl_dst[1] = DPDK_RING_STUB_DL_DST_1;
	flow->dl_dst[2] = DPDK_RING_STUB_DL_DST_2;
	flow->dl_dst[3] = DPDK_RING_STUB_DL_DST_3;
	flow->dl_dst[4] = DPDK_RING_STUB_DL_DST_4;
	flow->dl_dst[5] = DPDK_RING_STUB_DL_DST_5;
	flow->dl_src[0] = DPDK_RING_STUB_DL_SRC_0;
	flow->dl_src[1] = DPDK_RING_STUB_DL_SRC_1;
	flow->dl_src[2] = DPDK_RING_STUB_DL_SRC_2;
	flow->dl_src[3] = DPDK_RING_STUB_DL_SRC_3;
	flow->dl_src[4] = DPDK_RING_STUB_DL_SRC_4;
	flow->dl_src[5] = DPDK_RING_STUB_DL_SRC_5;
	flow->dl_type = rte_cpu_to_be_16(DPDK_RING_STUB_DL_TYPE);
}

void
test_dpif_dpdk_port_add(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct ovdk_message reply = {0};
	struct ovdk_message *request = NULL;
	int result = -1;
	struct netdev netdev;
	struct netdev * netdev_ptr;
	struct netdev_dpdk_phyport netdev_dpdk_phy;
	struct netdev_class netdev_class;
	odp_port_t port_no = -1;
	/* Hardcoded until we test with multiple pipelines */
	uint16_t pipeline_id = 1;

	/*
	 * Parameter checks
	 */

	/* Null in-parameter checks */
	result = dpif_p->dpif_class->port_add(dpif_p, NULL, &port_no);
	assert(result == EINVAL);
	result = dpif_p->dpif_class->port_add(dpif_p, &netdev, NULL);
	assert(result == EINVAL);

	/* Invalid netdev type */

	/* No need to enqueue a reply - we won't get that far */
	netdev.name = "invalid_port_example";
	netdev.netdev_class = &netdev_class;
	netdev_class.type = "doesnotexist";

	result = dpif_p->dpif_class->port_add(dpif_p, &netdev, &port_no);
	assert(result == ENODEV);

	/* Invalid vhost port name*/

	/* No need to enqueue a reply - we won't get that far */
	netdev.name = "an_extremely_long_port_name";
	netdev.netdev_class = &netdev_class;
	netdev_class.type = "dpdkvhost";

	result = dpif_p->dpif_class->port_add(dpif_p, &netdev, &port_no);
	assert(result == EINVAL);

	/*
	 * Default
	 */

	/* Bridge ports handled correctly */
	/* Create a fake reply to put on the reply ring. We don't use
	 * this, but transact will hang until a reply is received so
	 * there has to be something to dequeue.
	 */
	create_dpdk_port_reply(&reply, 0);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	netdev.name = "br0";
	netdev.netdev_class = &netdev_class;
	netdev_class.type = "internal";

	result = dpif_p->dpif_class->port_add(dpif_p, &netdev, &port_no);
	assert(result == 0);
	assert(port_no == OVDK_VPORT_TYPE_BRIDGE);
	result = dequeue_request_from_request_ring(&request, pipeline_id);
	assert(result == 0);
	assert(request->type == OVDK_VPORT_CMD_FAMILY);
	assert(request->vport_msg.cmd == VPORT_CMD_NEW);
	assert(request->vport_msg.flags == VPORT_FLAG_OUT_PORT );
	assert(request->vport_msg.vportid == OVDK_VPORT_TYPE_BRIDGE);
	assert(strncmp(request->vport_msg.port_name, "br0", OVDK_MAX_NAME_SIZE) == 0);
	assert(request->vport_msg.type == OVDK_VPORT_TYPE_BRIDGE);

	/* Non-bridge ports handled correctly */
	create_dpdk_port_reply(&reply, 0);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	netdev.name = "kni_example";
	netdev.netdev_class = &netdev_class;
	netdev_class.type = "dpdkkni";

	result = dpif_p->dpif_class->port_add(dpif_p, &netdev, &port_no);
	assert(result == 0);
	assert(port_no == OVDK_VPORT_TYPE_KNI);

	result = dequeue_request_from_request_ring(&request, pipeline_id);
	assert(result == 0);
	assert(request->type == OVDK_VPORT_CMD_FAMILY);
	assert(request->vport_msg.cmd == VPORT_CMD_NEW);
	assert(request->vport_msg.flags == VPORT_FLAG_INOUT_PORT );
	assert(request->vport_msg.vportid == OVDK_VPORT_TYPE_KNI);
	assert(strncmp(request->vport_msg.port_name, "kni_example", OVDK_MAX_NAME_SIZE) == 0);
	assert(request->vport_msg.type == OVDK_VPORT_TYPE_KNI);

	/* Invalid portmask handled correctly */
	/* Add a dpdkphy port. Then create a new reply and set error to -ENODEV,
	 * attempt to add another dpdkphy port. Check that ENODEV is present in reply.
	 */
	create_dpdk_port_reply(&reply, -ENODEV);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	netdev_ptr = (struct netdev *)&netdev_dpdk_phy;
	netdev_ptr->name = "phy_example_1";
	netdev_dpdk_phy.phy_dev.netdev_class = &netdev_class;
	netdev_class.type = "dpdkphy";
	netdev_dpdk_phy.port_id = OVDK_VPORT_TYPE_PHY;

	result = dpif_p->dpif_class->port_add(dpif_p, netdev_ptr, &port_no);
	assert(result == ENODEV);

	/* Failure from datapath */
	create_dpdk_port_reply(&reply, -1);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	netdev.name = "kni_example";
	netdev.netdev_class = &netdev_class;
	netdev_class.type = "dpdkkni";

	result = dpif_p->dpif_class->port_add(dpif_p, &netdev, &port_no);
	assert(result == 1);
}

void
test_dpif_dpdk_flow_put(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct ovdk_message reply = {0};
	struct ovdk_message *request = NULL;
	struct dpif_flow_put put = {0};
	struct flow flow;
	struct rte_mbuf *mbuf = NULL;
	void *ctrlmbuf_data = NULL;
	int result = -1;
	int num_pkts = 0;
	uint32_t vportid = 0;
	uint64_t handle = 0;

	/* Hardcoded to zero until we test with multiple pipelines */
	uint16_t pipeline_id = 1;

	/* Negative cases */

	/* Invalid paramater - NULL flow put message */
	result = dpif_p->dpif_class->flow_put(dpif_p, NULL);
	assert(result == EINVAL);

	/* Invalid parameter - put->key */
	create_dpif_flow_put_message(&put);
	put.key = NULL;
	result = dpif_p->dpif_class->flow_put(dpif_p, &put);
	assert(result == EINVAL);

	/* Port not correctly added */
	create_dpif_flow_put_message(&put);
	result = dpif_p->dpif_class->flow_put(dpif_p, &put);
	assert(result == EINVAL);

	/* Error from the datapath */

	/*
	 * Create a fake port entry. This is required by the
	 * flow_put() function in order to understand what lcore
	 * to request to.
	 *
	 * We request vportid 0.
	 */
	result = dpif_dpdk_vport_table_construct();
	assert(result == 0);
	vportid = 0;
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_PHY,
                                                 pipeline_id,
                                                 "fakeport",
                                                 &vportid);
	assert(result == 0);
	assert(vportid == 0);

	/* Create a fake reply to put on the reply. This will be an
	 * error reply.
	 */
	create_dpdk_flow_put_reply(&reply, -1);

	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);
	num_pkts = rte_ring_count(vswitchd_reply_ring[pipeline_id]);
	assert(num_pkts == 1);

	create_dpif_flow_put_message(&put);
	result = dpif_p->dpif_class->flow_put(dpif_p, &put);
	assert(result == 1);

	/*
	 * Ignore request we just want to check that it handles the reply
	 * correctly
	 */
	num_pkts = rte_ring_count(vswitchd_request_ring[pipeline_id]);
	assert(num_pkts == 1);
	result = rte_ring_sc_dequeue(vswitchd_request_ring[pipeline_id], (void **)&mbuf);
	assert(result == 0);

	/*
	 * Normal case
	 */

	/*
	 * Create a fake reply to put on the reply ring. We don't use
	 * this, but transact will hang until a reply is received so
	 * there has to be something to dequeue.
	 */
	create_dpdk_flow_put_reply(&reply, 0);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);
	num_pkts = rte_ring_count(vswitchd_reply_ring[pipeline_id]);
	assert(num_pkts == 1);

	create_dpif_flow_put_message(&put);
	result = dpif_p->dpif_class->flow_put(dpif_p, &put);
	assert(result == 0);

	num_pkts = rte_ring_count(vswitchd_request_ring[pipeline_id]);
	assert(num_pkts == 1);
	result = rte_ring_sc_dequeue(vswitchd_request_ring[pipeline_id], (void **)&mbuf);
	assert(result == 0);

	/* Just test that the message created and enqueued on the request ring
	 * was correct
	 */
	ctrlmbuf_data = rte_ctrlmbuf_data(mbuf);
	request = (struct ovdk_message *)ctrlmbuf_data;
	assert(request->flow_msg.actions[0].type == OVDK_ACTION_DROP);
	assert(request->flow_msg.key.in_port == DPDK_RING_STUB_IN_PORT);
	assert(request->flow_msg.key.ip_proto == DPDK_RING_STUB_NW_PROTO);
	assert(request->flow_msg.key.tran_src_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_SRC));
	assert(request->flow_msg.key.tran_dst_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_DST));
	assert(request->flow_msg.key.ip_src == rte_cpu_to_be_32(DPDK_RING_STUB_NW_SRC));
	assert(request->flow_msg.key.ip_dst == rte_cpu_to_be_32(DPDK_RING_STUB_NW_DST));
	//assert(request->flow_msg.key.vlan_tci == DPDK_RING_STUB_VLAN_TCI);
	assert(request->flow_msg.key.ether_dst.addr_bytes[0] == DPDK_RING_STUB_DL_DST_0);
	assert(request->flow_msg.key.ether_dst.addr_bytes[1] == DPDK_RING_STUB_DL_DST_1);
	assert(request->flow_msg.key.ether_dst.addr_bytes[2] == DPDK_RING_STUB_DL_DST_2);
	assert(request->flow_msg.key.ether_dst.addr_bytes[3] == DPDK_RING_STUB_DL_DST_3);
	assert(request->flow_msg.key.ether_dst.addr_bytes[4] == DPDK_RING_STUB_DL_DST_4);
	assert(request->flow_msg.key.ether_dst.addr_bytes[5] == DPDK_RING_STUB_DL_DST_5);
	assert(request->flow_msg.key.ether_src.addr_bytes[0] == DPDK_RING_STUB_DL_SRC_0);
	assert(request->flow_msg.key.ether_src.addr_bytes[1] == DPDK_RING_STUB_DL_SRC_1);
	assert(request->flow_msg.key.ether_src.addr_bytes[2] == DPDK_RING_STUB_DL_SRC_2);
	assert(request->flow_msg.key.ether_src.addr_bytes[3] == DPDK_RING_STUB_DL_SRC_3);
	assert(request->flow_msg.key.ether_src.addr_bytes[4] == DPDK_RING_STUB_DL_SRC_4);
	assert(request->flow_msg.key.ether_src.addr_bytes[5] == DPDK_RING_STUB_DL_SRC_5);
	assert(request->flow_msg.key.ether_type == rte_cpu_to_be_16(DPDK_RING_STUB_DL_TYPE));

	/* Ensure that entry was added to dpif flow table */
	populate_flow(&flow);
	result = dpif_dpdk_flow_table_entry_find(&flow, &handle);
	assert(result == 0);
	assert(handle == DPDK_RING_STUB_FLOW_HANDLE);

	rte_ctrlmbuf_free(mbuf);

}

void
test_dpif_dpdk_flow_del(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct ovdk_message reply;
	struct ovdk_message *request = NULL;
	struct dpif_flow_del del;
	struct rte_mbuf *mbuf = NULL;
	struct flow flow;
	void *ctrlmbuf_data = NULL;
	int result = -1;
	int num_pkts = 0;
	uint32_t vportid = 0;
	uint64_t handle = DPDK_RING_STUB_FLOW_HANDLE;
	/* Hardcoded to zero until we test with multiple pipelines */
	uint16_t pipeline_id = 1;

	/* Create a dpif flow table, and add an entry for the flow that we
	 * want to delete.
	 */
	result = dpif_dpdk_flow_table_construct();
	assert(result == 0);

	populate_flow(&flow);
	result = dpif_dpdk_flow_table_entry_add(&flow, &handle);
	assert(result == 0);

	/*
	 * Create a fake port entry. This is required by the
	 * flow_del() function in order to understand what lcore
	 * to request to.
	 *
	 * We request vportid 0.
	 */
	result = dpif_dpdk_vport_table_construct();
	assert(result == 0);
	vportid = 0;
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_PHY,
                                                 pipeline_id,
                                                "fakename",
                                                 &vportid);
	assert(result == 0);
	assert(vportid == 0);

	/* Create a fake reply to put on the reply ring. We don't use
	 * this, but transact will hang until a reply is received so
	 * there has to be something to dequeue.
	 */
	create_dpdk_flow_del_reply(&reply);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	create_dpif_flow_del_message(&del);
	dpif_p->dpif_class->flow_del(dpif_p, &del);
	num_pkts = rte_ring_count(vswitchd_request_ring[pipeline_id]);
	assert(num_pkts == 1);
	result = rte_ring_sc_dequeue(vswitchd_request_ring[pipeline_id], (void **)&mbuf);
	assert(result == 0);

	/*
	 * Test that the message created and enqueued on the request ring
	 * was correct, and that the stats were retrieved succesfully.
	 */
	ctrlmbuf_data = rte_ctrlmbuf_data(mbuf);
	request = (struct ovdk_message *)ctrlmbuf_data;

	assert(request->flow_msg.actions[0].type == OVDK_ACTION_NULL);
	assert(request->flow_msg.key.in_port == DPDK_RING_STUB_IN_PORT);
	assert(request->flow_msg.key.ip_proto == DPDK_RING_STUB_NW_PROTO);
	assert(request->flow_msg.key.tran_src_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_SRC));
	assert(request->flow_msg.key.tran_dst_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_DST));
	assert(request->flow_msg.key.ip_src == rte_cpu_to_be_32(DPDK_RING_STUB_NW_SRC));
	assert(request->flow_msg.key.ip_dst == rte_cpu_to_be_32(DPDK_RING_STUB_NW_DST));
	//assert(request->flow_msg.key.vlan_tci == DPDK_RING_STUB_VLAN_TCI);
	assert(request->flow_msg.key.ether_dst.addr_bytes[0] == DPDK_RING_STUB_DL_DST_0);
	assert(request->flow_msg.key.ether_dst.addr_bytes[1] == DPDK_RING_STUB_DL_DST_1);
	assert(request->flow_msg.key.ether_dst.addr_bytes[2] == DPDK_RING_STUB_DL_DST_2);
	assert(request->flow_msg.key.ether_dst.addr_bytes[3] == DPDK_RING_STUB_DL_DST_3);
	assert(request->flow_msg.key.ether_dst.addr_bytes[4] == DPDK_RING_STUB_DL_DST_4);
	assert(request->flow_msg.key.ether_dst.addr_bytes[5] == DPDK_RING_STUB_DL_DST_5);
	assert(request->flow_msg.key.ether_src.addr_bytes[0] == DPDK_RING_STUB_DL_SRC_0);
	assert(request->flow_msg.key.ether_src.addr_bytes[1] == DPDK_RING_STUB_DL_SRC_1);
	assert(request->flow_msg.key.ether_src.addr_bytes[2] == DPDK_RING_STUB_DL_SRC_2);
	assert(request->flow_msg.key.ether_src.addr_bytes[3] == DPDK_RING_STUB_DL_SRC_3);
	assert(request->flow_msg.key.ether_src.addr_bytes[4] == DPDK_RING_STUB_DL_SRC_4);
	assert(request->flow_msg.key.ether_src.addr_bytes[5] == DPDK_RING_STUB_DL_SRC_5);
	assert(request->flow_msg.key.ether_type     == rte_cpu_to_be_16(DPDK_RING_STUB_DL_TYPE));

	assert(request->flow_msg.flow_handle == DPDK_RING_STUB_FLOW_HANDLE);

	assert(del.stats->n_packets == DPDK_FLOW_STUB_STATS_PACKET_COUNT);
	assert(del.stats->n_bytes   == DPDK_FLOW_STUB_STATS_BYTE_COUNT);
	assert(del.stats->used      == DPDK_FLOW_STUB_STATS_USED);
	assert(del.stats->tcp_flags == DPDK_FLOW_STUB_STATS_TCP_FLAGS);

	/* Ensure that entry was deleted from dpif flow table */
	result = dpif_dpdk_flow_table_entry_find(&flow, &handle);
	assert(result == -ENOENT);

	rte_ctrlmbuf_free(mbuf);
}

void
test_dpif_dpdk_flow_del__null_key(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct dpif_flow_del del;
	int result = -1;

	create_dpif_flow_del_message(&del);
	del.key = NULL;
	result = dpif_p->dpif_class->flow_del(dpif_p, &del);
	assert(result == EINVAL);
}

void
test_dpif_dpdk_flow_del__null_dpif_flow_del(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	result = dpif_p->dpif_class->flow_del(dpif_p, NULL);
	assert(result == EINVAL);
}

void
test_dpif_dpdk_flow_del__flow_entry_not_found(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct dpif_flow_del del;
	int result = -1;
	uint32_t vportid = 0;
	/* Hardcoded to zero until we test with multiple pipelines */
	uint16_t pipeline_id = 1;

	/* Create a dpif flow table, and add an entry for the flow that we
	 * want to delete.
	 */
	result = dpif_dpdk_flow_table_construct();
	assert(result == 0);

	/*
	 * Create a fake port entry. This is required by the
	 * flow_del() function in order to understand what lcore
	 * to request to.
	 *
	 * We request vportid 0.
	 */
	result = dpif_dpdk_vport_table_construct();
	assert(result == 0);
	vportid = 0;
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_PHY,
                                                 pipeline_id,
                                                "fakename",
                                                 &vportid);
	assert(result == 0);
	assert(vportid == 0);

	create_dpif_flow_del_message(&del);
	result = dpif_p->dpif_class->flow_del(dpif_p, &del);
	assert(result == ENOENT);

}

void
test_dpif_dpdk_flow_get(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	const struct nlattr *key = NULL;
	size_t key_len = 0;
	struct ofpbuf **actionsp = NULL;
	struct dpif_flow_stats *stats = NULL;
	struct ovdk_message reply;
	struct ovdk_message *request = NULL;
	void *ctrlmbuf_data = NULL;
	struct rte_mbuf *mbuf = NULL;
	uint64_t handle = DPDK_RING_STUB_FLOW_HANDLE;
	struct dpif_flow_put get;
	struct flow flow;
	uint32_t vportid = 0;
	/* Hardcoded to zero until we test with multiple pipelines */
	uint16_t pipeline_id = 1;

	/* Reserve memory for stats & actions pointers */
	stats = malloc(sizeof(*stats));
	actionsp = malloc(sizeof(**actionsp));

	/* Create a dpif flow table, and add an entry for the flow that we
	 * want to get.
	 */
	result = dpif_dpdk_flow_table_construct();
	assert(result == 0);

	populate_flow(&flow);
	result = dpif_dpdk_flow_table_entry_add(&flow, &handle);
	assert(result == 0);

	/*
	 * Create a dpif vport table, and add a fake entry for vportid 0.
	 * This is required by the flow_get() function in order to understand what
	 * lcore to request to.
	 */
	result = dpif_dpdk_vport_table_construct();
	assert(result == 0);
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_PHY,
	                                         pipeline_id,
	                                         "fakename",
	                                         &vportid);
	assert(result == 0);
	assert(vportid == 0);

	/* Generate key equivalent to the flow we're looking for */
	create_dpif_flow_get_message(&get);
	key = get.key;
	key_len = get.key_len;


	/* Create a fake reply to put on the reply ring.
	 * flow_get() uses this reply to update the stats and actions pointers
	 * passed to the function.
	 */
	create_dpdk_flow_get_reply(&reply);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	result = dpif_p->dpif_class->flow_get(dpif_p, key, key_len, actionsp, stats);
	assert(result == 0);

	/*
	 * Test that the message created and enqueued on the request ring
	 * was correct.
	 */

	result = rte_ring_sc_dequeue(vswitchd_request_ring[pipeline_id],
								(void **)&mbuf);
	assert(result == 0);

	ctrlmbuf_data = rte_ctrlmbuf_data(mbuf);
	request = (struct ovdk_message *)ctrlmbuf_data;

	assert(request->flow_msg.cmd == OVS_FLOW_CMD_GET);

	assert(request->flow_msg.key.in_port == DPDK_RING_STUB_IN_PORT);
	assert(request->flow_msg.key.ip_proto == DPDK_RING_STUB_NW_PROTO);
	assert(request->flow_msg.key.tran_src_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_SRC));
	assert(request->flow_msg.key.tran_dst_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_DST));
	assert(request->flow_msg.key.ip_src == rte_cpu_to_be_32(DPDK_RING_STUB_NW_SRC));
	assert(request->flow_msg.key.ip_dst == rte_cpu_to_be_32(DPDK_RING_STUB_NW_DST));
	//assert(request->flow_msg.key.vlan_tci == DPDK_RING_STUB_VLAN_TCI);
	assert(request->flow_msg.key.ether_dst.addr_bytes[0] == DPDK_RING_STUB_DL_DST_0);
	assert(request->flow_msg.key.ether_dst.addr_bytes[1] == DPDK_RING_STUB_DL_DST_1);
	assert(request->flow_msg.key.ether_dst.addr_bytes[2] == DPDK_RING_STUB_DL_DST_2);
	assert(request->flow_msg.key.ether_dst.addr_bytes[3] == DPDK_RING_STUB_DL_DST_3);
	assert(request->flow_msg.key.ether_dst.addr_bytes[4] == DPDK_RING_STUB_DL_DST_4);
	assert(request->flow_msg.key.ether_dst.addr_bytes[5] == DPDK_RING_STUB_DL_DST_5);
	assert(request->flow_msg.key.ether_src.addr_bytes[0] == DPDK_RING_STUB_DL_SRC_0);
	assert(request->flow_msg.key.ether_src.addr_bytes[1] == DPDK_RING_STUB_DL_SRC_1);
	assert(request->flow_msg.key.ether_src.addr_bytes[2] == DPDK_RING_STUB_DL_SRC_2);
	assert(request->flow_msg.key.ether_src.addr_bytes[3] == DPDK_RING_STUB_DL_SRC_3);
	assert(request->flow_msg.key.ether_src.addr_bytes[4] == DPDK_RING_STUB_DL_SRC_4);
	assert(request->flow_msg.key.ether_src.addr_bytes[5] == DPDK_RING_STUB_DL_SRC_5);
	assert(request->flow_msg.key.ether_type == rte_cpu_to_be_16(DPDK_RING_STUB_DL_TYPE));

	assert(request->flow_msg.flow_handle == DPDK_RING_STUB_FLOW_HANDLE);

	/* Check that stats were updated */
	assert(stats->n_packets == DPDK_FLOW_STUB_STATS_PACKET_COUNT);
	assert(stats->n_bytes == DPDK_FLOW_STUB_STATS_BYTE_COUNT);
	assert(stats->used == DPDK_FLOW_STUB_STATS_USED);
	assert(stats->tcp_flags == DPDK_FLOW_STUB_STATS_TCP_FLAGS);

	free(stats);
	free(actionsp);
}

void
test_dpif_dpdk_flow_get__null_key(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	const struct nlattr *key = NULL;
	size_t key_len = 0;
	struct ofpbuf **actionsp = NULL;
	struct dpif_flow_stats *stats = NULL;

	result = dpif_p->dpif_class->flow_get(dpif_p, key, key_len, actionsp, stats);
	assert(result == EINVAL);
}

void
test_dpif_dpdk_flow_get__flow_entry_not_found(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	const struct nlattr *key = NULL;
	size_t key_len = 0;
	struct ofpbuf **actionsp = NULL;
	struct dpif_flow_stats *stats = NULL;
	struct ovdk_message reply;
	struct dpif_flow_put get;
	uint32_t vportid = 0;
	/* Hardcoded to zero until we test with multiple pipelines */
	uint16_t pipeline_id = 1;

	/* Reserve memory for stats & actions pointers */
	stats = malloc(sizeof(*stats));
	actionsp = malloc(sizeof(**actionsp));

	/* Create a dpif flow table with zero entries */
	result = dpif_dpdk_flow_table_construct();
	assert(result == 0);

	/*
	 * Create a dpif vport table, and add a fake entry for vportid 0.
	 * This is required by the flow_get() function in order to understand what
	 * lcore to request to.
	 */
	result = dpif_dpdk_vport_table_construct();
	assert(result == 0);
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_PHY,
	                                         pipeline_id,
	                                        "fakename",
	                                         &vportid);
	assert(result == 0);
	assert(vportid == 0);

	/* Generate key equivalent to the flow we're looking for */
	create_dpif_flow_get_message(&get);
	key = get.key;
	key_len = get.key_len;

	/* Create a fake reply to put on the reply ring.
	 * flow_get() uses this reply to update the stats and actions pointers
	 * passed to the function.
	 */
	create_dpdk_flow_get_reply(&reply);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	result = dpif_p->dpif_class->flow_get(dpif_p, key, key_len, actionsp, stats);
	assert(result == ENOENT);	/* Entry not found */

	free(stats);
	free(actionsp);
}

void
test_dpif_dpdk_flow_dump_start(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{

	struct dpif_dpdk_flow_state *state = NULL;
	int result = 0;

	result = dpif_p->dpif_class->flow_dump_start(dpif_p, (void **)&state);
	assert(result == 0);
	assert(state->flow.cmd == OVS_FLOW_CMD_GET);
	assert(state->flow_table_index == UINT16_MAX);
}


void
test_dpif_dpdk_flow_dump_next(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct dpif_dpdk_flow_state *state = NULL;
	const struct nlattr **key = NULL;
	size_t *key_len = NULL;
	const struct nlattr **mask = NULL;
	size_t *mask_len = NULL;
	const struct nlattr **actions = NULL;
	size_t *actions_len = NULL;
	const struct dpif_flow_stats **stats = NULL;
	int result = 0;
	struct ovdk_message reply;
	uint64_t handle = DPDK_RING_STUB_FLOW_HANDLE;
	struct flow flow;
	uint32_t vportid = 0;
	/* Hardcoded to zero until we test with multiple pipelines */
	uint16_t pipeline_id = 1;

	/* Reserve memory for stats, actions, key and mask pointers */
	stats = malloc(sizeof(**stats));
	actions = malloc(sizeof(**actions));
	actions_len = malloc(sizeof(*actions_len));
	key = malloc(sizeof(**key));
	key_len = malloc(sizeof(*key_len));
	mask = malloc(sizeof(**mask));
	mask_len = malloc(sizeof(*mask_len));

	/* Initialise state via call to dump_start() */
	result = dpif_p->dpif_class->flow_dump_start(dpif_p, (void **)&state);
	assert(result == 0);

	/* Create a dpif flow table, and add an entry for the flow that we
	 * want to dump.
	 */
	result = dpif_dpdk_flow_table_construct();
	assert(result == 0);

	populate_flow(&flow);
	result = dpif_dpdk_flow_table_entry_add(&flow, &handle);
	assert(result == 0);

	/*
	 * Create a dpif vport table, and add a fake entry for vportid 0.
	 * This is required by the flow_dump() function in order to understand what
	 * lcore to request to.
	 */
	result = dpif_dpdk_vport_table_construct();
	assert(result == 0);
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_PHY,
	                                         pipeline_id,
                                             "fakename",
	                                         &vportid);
	assert(result == 0);
	assert(vportid == 0);

	/* Create a fake reply to put on the reply ring.
	 * flow_dump_next() uses this reply to update the stats and actions pointers
	 * passed to the function.
	 */
	create_dpdk_flow_get_reply(&reply);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	result = dpif_p->dpif_class->flow_dump_next(dpif_p, state,
												key, key_len,
												mask, mask_len,
												actions, actions_len,
												stats);

	assert(state->flow.flow_handle == handle);
	assert(state->flow.key.in_port == DPDK_RING_STUB_IN_PORT);
	assert(state->flow.key.ip_proto == DPDK_RING_STUB_NW_PROTO);
	assert(state->flow.key.tran_src_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_SRC));
	assert(state->flow.key.tran_dst_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_DST));
	assert(state->flow.key.ip_src == rte_cpu_to_be_32(DPDK_RING_STUB_NW_SRC));
	assert(state->flow.key.ip_dst == rte_cpu_to_be_32(DPDK_RING_STUB_NW_DST));
	//assert(state->flow.key.vlan_tci == DPDK_RING_STUB_VLAN_TCI);
	assert(state->flow.key.ether_dst.addr_bytes[0] == DPDK_RING_STUB_DL_DST_0);
	assert(state->flow.key.ether_dst.addr_bytes[1] == DPDK_RING_STUB_DL_DST_1);
	assert(state->flow.key.ether_dst.addr_bytes[2] == DPDK_RING_STUB_DL_DST_2);
	assert(state->flow.key.ether_dst.addr_bytes[3] == DPDK_RING_STUB_DL_DST_3);
	assert(state->flow.key.ether_dst.addr_bytes[4] == DPDK_RING_STUB_DL_DST_4);
	assert(state->flow.key.ether_dst.addr_bytes[5] == DPDK_RING_STUB_DL_DST_5);
	assert(state->flow.key.ether_src.addr_bytes[0] == DPDK_RING_STUB_DL_SRC_0);
	assert(state->flow.key.ether_src.addr_bytes[1] == DPDK_RING_STUB_DL_SRC_1);
	assert(state->flow.key.ether_src.addr_bytes[2] == DPDK_RING_STUB_DL_SRC_2);
	assert(state->flow.key.ether_src.addr_bytes[3] == DPDK_RING_STUB_DL_SRC_3);
	assert(state->flow.key.ether_src.addr_bytes[4] == DPDK_RING_STUB_DL_SRC_4);
	assert(state->flow.key.ether_src.addr_bytes[5] == DPDK_RING_STUB_DL_SRC_5);
	assert(state->flow.key.ether_type     == rte_cpu_to_be_16(DPDK_RING_STUB_DL_TYPE));

	assert(result == 0);

	/* Check actions updated correctly */
	assert(*actions == state->actions_buf.data);
	assert(*actions_len == state->actions_buf.size);

	/* Check key updated correctly */
	assert(*key == state->key_buf.data);
	assert(*key_len == state->key_buf.size);

	/* Check stats updated correctly */
	assert(*stats == &state->stats);

	/* Check mask updated correctly */
	assert(*mask == NULL);
	assert(*mask_len == 0);

	free(stats);
	free(key);
	free(key_len);
	free(mask);
	free(mask_len);
	free(actions);
	free(actions_len);
}

void
test_dpif_dpdk_flow_dump_next__null_state(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct dpif_dpdk_flow_state *state = NULL;
	const struct nlattr **key = NULL;
	size_t *key_len = NULL;
	const struct nlattr **mask = NULL;
	size_t *mask_len = NULL;
	const struct nlattr **actions = NULL;
	size_t *actions_len = NULL;
	const struct dpif_flow_stats **stats = NULL;
	int result = 0;

	result = dpif_p->dpif_class->flow_dump_next(dpif_p, state,
												key, key_len,
												mask, mask_len,
												actions, actions_len,
												stats);

	assert(result == EINVAL);
}

void
test_dpif_dpdk_flow_dump_next__no_entry(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct dpif_dpdk_flow_state *state = NULL;
	const struct nlattr **key = NULL;
	size_t *key_len = NULL;
	const struct nlattr **mask = NULL;
	size_t *mask_len = NULL;
	const struct nlattr **actions = NULL;
	size_t *actions_len = NULL;
	const struct dpif_flow_stats **stats = NULL;
	int result = 0;
	struct ovdk_message reply = {0};
	/* Hardcoded to zero until we test with multiple pipelines */
	uint16_t pipeline_id = 1;

	/* Initialise state via call to dump_start() */
	result = dpif_p->dpif_class->flow_dump_start(dpif_p, (void **)&state);
	assert(result == 0);

	/* Create a fake reply to put on the reply ring.
	 * flow_dump_next() uses this reply to update the stats and actions pointers
	 * passed to the function.
	 */
	create_dpdk_flow_get_reply(&reply);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	result = dpif_p->dpif_class->flow_dump_next(dpif_p, state,
												key, key_len,
												mask, mask_len,
												actions, actions_len,
												stats);
	assert(result == EOF);	/* No entry found */
}

void
test_dpif_dpdk_flow_dump_done(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	struct dpif_dpdk_flow_state *state = NULL;

	/* Reserve memory for state pointer */
	state = malloc(sizeof(*state));

	result = dpif_p->dpif_class->flow_dump_done(dpif_p, state);
	assert(result == 0);
}

void
test_dpif_dpdk_flow_dump_done__null_state(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	struct dpif_dpdk_flow_state *state = NULL;

	/* Test null state */
	result = dpif_p->dpif_class->flow_dump_done(dpif_p, state);
	assert(result == EINVAL);
}

void
test_dpif_dpdk_flow_flush(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	struct ovdk_message reply = {0};
	struct ovdk_message *request = NULL;
	void *ctrlmbuf_data = NULL;
	struct rte_mbuf *mbuf = NULL;
	uint64_t handle = DPDK_RING_STUB_FLOW_HANDLE;
	uint64_t handle_2 = DPDK_RING_STUB_FLOW_HANDLE+1;
	uint32_t vportid = 0;
	struct flow flow;
	struct flow flow_2;
	int i = 0;
	/* Hardcoded to zero until we test with multiple pipelines */
	uint16_t pipeline_id = 1;

	/* Create a dpif flow table, and add two entries for the flows that we
	 * want to flush.
	 */
	result = dpif_dpdk_flow_table_construct();
	assert(result == 0);

	populate_flow(&flow);
	populate_flow(&flow_2);

	/* Change one field of the second flow so that flow_table_entry_add doesn't
	 * fail due to existing flow
	 */
	flow_2.dl_dst[0] = DPDK_RING_STUB_DL_DST_1;

	result = dpif_dpdk_flow_table_entry_add(&flow, &handle);
	assert(result == 0);
	result = dpif_dpdk_flow_table_entry_add(&flow_2, &handle_2);
	assert(result == 0);

	/*
	 * Create a dpif vport table, and add a fake entry for vportid 0.
	 * This is required by the flow_flush() function in order to understand what
	 * lcore to request to.
	 */
	result = dpif_dpdk_vport_table_construct();
	assert(result == 0);
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_PHY,
											 pipeline_id,
											 "fakename",
											 &vportid);
	assert(result == 0);
	assert(vportid == 0);

	/* Check that entries have been added correctly, before we flush */
	result = dpif_dpdk_flow_table_entry_find(&flow, &handle);
	assert(result == 0);
	result = dpif_dpdk_flow_table_entry_find(&flow_2, &handle_2);
	assert(result == 0);

	/* Create 2 fake replies to put on the reply ring. We don't use
	 * these, but transact will hang until a reply is received so
	 * there has to be something to dequeue.
	 */
	create_dpdk_flow_get_reply(&reply);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	result = dpif_p->dpif_class->flow_flush(dpif_p);

	/*
	 * Test that the messages created and enqueued on the request ring
	 * were correct.
	 */
	for (i = 0; i < 2; i++) {
		result = rte_ring_sc_dequeue(vswitchd_request_ring[pipeline_id],
									(void **)&mbuf);
		assert(result == 0);

		ctrlmbuf_data = rte_ctrlmbuf_data(mbuf);
		request = (struct ovdk_message *)ctrlmbuf_data;

		assert(request->flow_msg.cmd == OVS_FLOW_CMD_DEL);
		assert(request->flow_msg.key.in_port == DPDK_RING_STUB_IN_PORT);
		assert(request->flow_msg.key.ip_proto == DPDK_RING_STUB_NW_PROTO);
		assert(request->flow_msg.key.tran_src_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_SRC));
		assert(request->flow_msg.key.tran_dst_port == rte_cpu_to_be_16(DPDK_RING_STUB_TP_DST));
		assert(request->flow_msg.key.ip_src == rte_cpu_to_be_32(DPDK_RING_STUB_NW_SRC));
		assert(request->flow_msg.key.ip_dst == rte_cpu_to_be_32(DPDK_RING_STUB_NW_DST));
		if (i == 0)
			assert(request->flow_msg.key.ether_dst.addr_bytes[0] == DPDK_RING_STUB_DL_DST_0);
		else
			assert(request->flow_msg.key.ether_dst.addr_bytes[0] == DPDK_RING_STUB_DL_DST_1);
		assert(request->flow_msg.key.ether_dst.addr_bytes[1] == DPDK_RING_STUB_DL_DST_1);
		assert(request->flow_msg.key.ether_dst.addr_bytes[2] == DPDK_RING_STUB_DL_DST_2);
		assert(request->flow_msg.key.ether_dst.addr_bytes[3] == DPDK_RING_STUB_DL_DST_3);
		assert(request->flow_msg.key.ether_dst.addr_bytes[4] == DPDK_RING_STUB_DL_DST_4);
		assert(request->flow_msg.key.ether_dst.addr_bytes[5] == DPDK_RING_STUB_DL_DST_5);
		assert(request->flow_msg.key.ether_src.addr_bytes[0] == DPDK_RING_STUB_DL_SRC_0);
		assert(request->flow_msg.key.ether_src.addr_bytes[1] == DPDK_RING_STUB_DL_SRC_1);
		assert(request->flow_msg.key.ether_src.addr_bytes[2] == DPDK_RING_STUB_DL_SRC_2);
		assert(request->flow_msg.key.ether_src.addr_bytes[3] == DPDK_RING_STUB_DL_SRC_3);
		assert(request->flow_msg.key.ether_src.addr_bytes[4] == DPDK_RING_STUB_DL_SRC_4);
		assert(request->flow_msg.key.ether_src.addr_bytes[5] == DPDK_RING_STUB_DL_SRC_5);
		assert(request->flow_msg.key.ether_type == rte_cpu_to_be_16(DPDK_RING_STUB_DL_TYPE));
		if (i == 0)
			assert(request->flow_msg.flow_handle == handle);
		else
			assert(request->flow_msg.flow_handle == handle_2);
	}

	/* Check that entries have been deleted correctly, after the flush */
	result = dpif_dpdk_flow_table_entry_find(&flow, &handle);
	assert(result == -ENOENT);
	result = dpif_dpdk_flow_table_entry_find(&flow_2, &handle_2);
	assert(result == -ENOENT);
}

void
test_dpif_dpdk_get_stats(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct dpif_dp_stats stats;
	result = dpif_p->dpif_class->get_stats(dpif_p, &stats);
	assert(result == 0);
	assert(stats.n_hit == 0);
	assert(stats.n_missed == 0);
	assert(stats.n_lost == 0);
	assert(stats.n_flows == 0);
}

void
test_dpif_dpdk_port_del(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct ovdk_message reply;
	struct ovdk_message *request = NULL;
	int result = -1;
	odp_port_t port_no = -1;
	unsigned pipeline_id = 1;

	/* - Input parameter check - */
	result = dpif_p->dpif_class->port_del(dpif_p, port_no);
	assert(result == EINVAL); /* port_no = -1 */

	port_no = OVDK_VPORT_TYPE_CLIENT;
	/* Add fake entry to vport table */
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_CLIENT, pipeline_id, "dummyname", &port_no);
	assert(result == 0);

	/* - Error codes passed back from datapath - */
	/* Build fake error reply */
	create_dpdk_port_reply(&reply, -1);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);
	result = dpif_p->dpif_class->port_del(dpif_p, port_no);
	assert(result == 1);
	/*
	 * Check entry was correctly removed from the table
	 * (table should be empty)
	 */
	result = dpif_dpdk_vport_table_entry_get_first_inuse(&port_no);
	assert(result == -ENOENT);

	/* - Normal operation - */
	/* Add fake entry to vport table */
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_CLIENT, pipeline_id, "dummyname", &port_no);
	assert(result == 0);
	/* Build fake successful reply */
	create_dpdk_port_reply(&reply, 0);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);
	result = dpif_p->dpif_class->port_del(dpif_p, port_no);
	assert(result == 0);

	result = dequeue_request_from_request_ring(&request, pipeline_id);
	assert(result == 0);
	assert(request->vport_msg.cmd == VPORT_CMD_DEL);
	assert(request->vport_msg.flags == 0 );
	assert(request->vport_msg.vportid == OVDK_VPORT_TYPE_CLIENT);
	/*
	 * Check entry was correctly removed from the table
	 * (table should be empty)
	 */
	result = dpif_dpdk_vport_table_entry_get_first_inuse(&port_no);
	assert(result == -ENOENT);
}

void
test_dpif_dpdk_port_query_by_number(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct dpif_port dpif_port;
	struct dpif dpif;
	char port_name[OVDK_MAX_NAME_SIZE] = "random_port";
	enum ovdk_vport_type type = OVDK_VPORT_TYPE_DISABLED;
	odp_port_t port_no = 0;

	/*
	 * Invalid parameters
	 */

	/* Null dpif */
	result = dpif_p->dpif_class->port_query_by_number(NULL, port_no,
	                                                  &dpif_port);
	assert(result == EINVAL);
	/* Invalid port_no */
	result = dpif_p->dpif_class->port_query_by_number(&dpif,
	                                                  OVDK_MAX_VPORTS,
	                                                  &dpif_port);
	assert(result == EINVAL);

	/*
	 * Default
	 */

	/* normal operation */
	type = OVDK_VPORT_TYPE_CLIENT;

	/* add a sample entry to table */
	result = dpif_dpdk_vport_table_entry_add(type, 0, &port_name[0],
	                                         &port_no);
	assert(result == 0);

	/* this should return the above sample entry */
	result = dpif_p->dpif_class->port_query_by_number(&dpif, port_no,
	                                                  &dpif_port);
	assert(result == 0);
	assert(strcmp(dpif_port.name, &port_name[0]) == 0);
	assert(strcmp(dpif_port.type, "dpdkclient") == 0);
	assert(dpif_port.port_no == port_no);
}

void
test_dpif_dpdk_port_query_by_name(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct dpif_port dpif_port;
	struct dpif dpif;
	char port_name[OVDK_MAX_NAME_SIZE] = "random_port";
	enum ovdk_vport_type type = OVDK_VPORT_TYPE_DISABLED;
	odp_port_t port_no = 0;

	/*
	 * Invalid parameters
	 */

	/* Null dpif */
	result = dpif_p->dpif_class->port_query_by_name(NULL, &port_name[0],
	                                                &dpif_port);
	assert(result == EINVAL);
	/* Null name */
	result = dpif_p->dpif_class->port_query_by_name(&dpif, NULL,
	                                                &dpif_port);
	assert(result == EINVAL);

	/*
	 * Default
	 */

	/* normal operation */
	type = OVDK_VPORT_TYPE_CLIENT;

	/* add a sample entry to table */
	result = dpif_dpdk_vport_table_entry_add(type, 0, &port_name[0],
	                                         &port_no);
	assert(result == 0);

	/* this should return the above sample entry */
	result = dpif_p->dpif_class->port_query_by_name(&dpif, &port_name[0],
	                                                &dpif_port);
	assert(result == 0);
	assert(strcmp(dpif_port.name, &port_name[0]) == 0);
	assert(strcmp(dpif_port.type, "dpdkclient") == 0);
	assert(dpif_port.port_no == port_no);

	/* null dpif port */
	result = dpif_p->dpif_class->port_query_by_name(&dpif, &port_name[0],
	                                                NULL);
	assert(result == 0);
}

void
test_dpif_dpdk_get_max_ports(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	result = dpif_p->dpif_class->get_max_ports(dpif_p);
	assert(result == OVDK_MAX_VPORTS);
}

void
test_dpif_dpdk_port_dump_start(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct dpif dpif;
	struct dpif_dpdk_port_state *state;
	void **statep = (void **)&state;

	/*
	 * Invalid parameters
	 */

	/* Invalid dpif */
	result = dpif_p->dpif_class->port_dump_start(NULL, statep);
	assert(result == EINVAL);
	/* Invalid state */
	result = dpif_p->dpif_class->port_dump_start(&dpif, NULL);
	assert(result == EINVAL);

	/*
	 * Default
	 */

	/* normal operation */
	result = dpif_p->dpif_class->port_dump_start(&dpif, statep);
	assert(result == 0);
}

void
test_dpif_dpdk_port_dump_next(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct dpif_port dpif_port;
	struct dpif dpif;
	struct dpif_dpdk_port_state *state;
	char port_name1[OVDK_MAX_NAME_SIZE] = "random_port1";
	char port_name2[OVDK_MAX_NAME_SIZE] = "random_port2";
	char port_name3[OVDK_MAX_NAME_SIZE] = "random_port3";
	uint32_t port_no1 = OVDK_VPORT_TYPE_PHY;
	uint32_t port_no2 = OVDK_VPORT_TYPE_CLIENT;
	uint32_t port_no3 = OVDK_VPORT_TYPE_VHOST;
	enum ovdk_vport_type port_type1 = OVDK_VPORT_TYPE_PHY;
	enum ovdk_vport_type port_type2 = OVDK_VPORT_TYPE_CLIENT;
	enum ovdk_vport_type port_type3 = OVDK_VPORT_TYPE_VHOST;
	char type_name1[OVDK_MAX_NAME_SIZE] = "dpdkphy";
	char type_name2[OVDK_MAX_NAME_SIZE] = "dpdkclient";
	char type_name3[OVDK_MAX_NAME_SIZE] = "dpdkvhost";

	/* dump is a state machine, and hence relies on dump_start to get things
	 * rolling. Might as well use that function here to set things up */
	result = dpif_p->dpif_class->port_dump_start(&dpif, (void **)&state);
	assert(result == 0);

	/*
	 * Invalid parameters
	 */

	/* Invalid dpif */
	result = dpif_p->dpif_class->port_dump_next(NULL, state, &dpif_port);
	assert(result == EINVAL);
	/* Invalid state */
	result = dpif_p->dpif_class->port_dump_next(&dpif, NULL, &dpif_port);
	assert(result == EINVAL);
	/* Invalid dpif_port */
	result = dpif_p->dpif_class->port_dump_next(&dpif, state, NULL);
	assert(result == EINVAL);

	/*
	 * Default
	 */

	/* normal operation */
	/* Add a few random entries */
	result = dpif_dpdk_vport_table_entry_add(port_type1, 0, &port_name1[0], &port_no1);
	assert(result == 0);
	result = dpif_dpdk_vport_table_entry_add(port_type2, 0, &port_name2[0], &port_no2);
	assert(result == 0);
	result = dpif_dpdk_vport_table_entry_add(port_type3, 0, &port_name3[0], &port_no3);
	assert(result == 0);
	/* Check dumped ports are same as the entries that were added */
	result = dpif_p->dpif_class->port_dump_next(&dpif, state, &dpif_port);
	assert(result == 0);
	assert(strcmp(dpif_port.type,type_name1) == 0);
	assert(dpif_port.port_no == port_no1);
	assert(strcmp(dpif_port.name, port_name1) == 0);
	result = dpif_p->dpif_class->port_dump_next(&dpif, state, &dpif_port);
	assert(result == 0);
	assert(strcmp(dpif_port.type,type_name2) == 0);
	assert(dpif_port.port_no == port_no2);
	assert(strcmp(dpif_port.name, port_name2) == 0);
	result = dpif_p->dpif_class->port_dump_next(&dpif, state, &dpif_port);
	assert(result == 0);
	assert(strcmp(dpif_port.type,type_name3) == 0);
	assert(dpif_port.port_no == port_no3);
	assert(strcmp(dpif_port.name, port_name3) == 0);
}

void
test_dpif_dpdk_port_dump_done(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	void *state_done = NULL;

	/*
	 * note: state_done is null here, but port dump done doesn't
	 * use it so it doesn't matter
	 */
	result = dpif_p->dpif_class->port_dump_done(dpif_p, state_done);
	assert(result == 0);
}

void
test_dpif_dpdk_port_poll(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	char **devname = NULL;
	int result = -1;
	/* note: devname is null here, but port_poll doesn't
	 * use it so it doesn't matter
	 */
	result = dpif_p->dpif_class->port_poll(dpif_p, devname);
	assert(result == EAGAIN);
}

void
test_dpif_dpdk_port_poll_wait(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	/* note: devname is null here, but port poll wait doesn't
	 * use it so it doesn't matter
	 */
	dpif_p->dpif_class->port_poll_wait(dpif_p);
}

void
test_dpif_dpdk_port_get_stats(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	char name[OVDK_MAX_VPORT_NAMESIZE] = "fakename";
	struct ovdk_message reply = {0};
	uint32_t vportid = 0;
	enum ovdk_vport_type type = OVDK_VPORT_TYPE_PHY;
	struct ovdk_port_stats stats = {0};
	int result = 0;
	unsigned pipeline_id = 1;

	/* invalid params */

	/* invalid name */
	result = dpif_dpdk_port_get_stats(NULL, &stats);
	assert(result == EINVAL);

	/* invalid stats */
	result = dpif_dpdk_port_get_stats(&name[0], NULL);
	assert(result == EINVAL);

	/* invalid reply from datapath */
	/* Add a random entry */
	result = dpif_dpdk_vport_table_entry_add(type, pipeline_id, &name[0], &vportid);
	assert(result == 0);
	/* put an invalid entry on the reply ring */
	create_dpdk_port_reply(&reply, -1);
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	result = dpif_dpdk_port_get_stats(&name[0], &stats);
	assert(result == 1);

	/* normal case */

	/* Create a fake reply and set the stats to some arbitrary values */
	create_dpdk_port_reply(&reply, 0);
	reply.vport_msg.stats.rx = 0xDEADBEEF;
	reply.vport_msg.stats.tx = 0xDEADBEEF;
	reply.vport_msg.stats.rx_bytes = 0xDEADBEEF;
	reply.vport_msg.stats.tx_bytes = 0xDEADBEEF;
	reply.vport_msg.stats.rx_drop = 0xDEADBEEF;
	reply.vport_msg.stats.tx_drop = 0xDEADBEEF;
	reply.vport_msg.stats.rx_error = 0xDEADBEEF;
	reply.vport_msg.stats.tx_error = 0xDEADBEEF;
	result = enqueue_reply_on_reply_ring(reply, pipeline_id);
	assert(result == 0);

	result = dpif_dpdk_port_get_stats(&name[0], &stats);
	assert(result == 0);
	assert(stats.rx == 0xDEADBEEF);
	assert(stats.tx == 0xDEADBEEF);
	assert(stats.rx_bytes == 0xDEADBEEF);
	assert(stats.tx_bytes == 0xDEADBEEF);
	assert(stats.rx_drop == 0xDEADBEEF);
	assert(stats.tx_drop == 0xDEADBEEF);
	assert(stats.rx_error == 0xDEADBEEF);
	assert(stats.tx_error == 0xDEADBEEF);
}

void
test_dpif_dpdk_recv(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct dpif_upcall upcall = {0};
	struct ofpbuf ofpbuf = {0};
	int result = 0;
	unsigned pipeline_id = 1;
	struct flow flow;
	enum odp_key_fitness fitness_error = ODP_FIT_ERROR;

	/* invalid params */

	/* invalid dpif */
	result = dpif_p->dpif_class->recv(NULL, &upcall, &ofpbuf);
	assert(result == EINVAL);

	/* invalid upcall */
	result = dpif_p->dpif_class->recv(dpif_p, NULL, &ofpbuf);
	assert(result == EINVAL);

	/* invalid ofpbuf */
	result = dpif_p->dpif_class->recv(dpif_p, &upcall, NULL);
	assert(result == EINVAL);

	/* invalid upcall */
	/* Enqueue fake upcall */
	result = enqueue_upcall_on_exception_ring(42, pipeline_id);
	assert(result == 0);
	result = dpif_p->dpif_class->recv(dpif_p, &upcall, &ofpbuf);
	assert(result == EINVAL);

	/* Normal operation */

	/* Enqueue fake upcall */
	result = enqueue_upcall_on_exception_ring(OVS_PACKET_CMD_MISS, pipeline_id);
	assert(result == 0);
	result = dpif_p->dpif_class->recv(dpif_p, &upcall, &ofpbuf);
	assert(result == 0);
	assert(upcall.type == DPIF_UC_MISS);
        fitness_error = odp_flow_key_to_flow(upcall.key, upcall.key_len, &flow);
	assert(fitness_error != ODP_FIT_ERROR);
	assert(flow.in_port.odp_port == DPDK_RING_STUB_IN_PORT);
	assert(flow.nw_proto == DPDK_RING_STUB_NW_PROTO);
	assert(flow.tp_src == rte_cpu_to_be_16(DPDK_RING_STUB_TP_SRC));
	assert(flow.tp_dst == rte_cpu_to_be_16(DPDK_RING_STUB_TP_DST));
	assert(flow.nw_src == rte_cpu_to_be_32(DPDK_RING_STUB_NW_SRC));
	assert(flow.nw_dst == rte_cpu_to_be_32(DPDK_RING_STUB_NW_DST));
	assert(flow.vlan_tci == rte_cpu_to_be_16(DPDK_RING_STUB_VLAN_TCI));
	assert(flow.dl_type == rte_cpu_to_be_16(DPDK_RING_STUB_DL_TYPE));
}

void
test_dpif_dpdk_execute(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct dpif_execute execute = {0};
	int result = 0;
	unsigned pipeline_id = 1;
	odp_port_t port_no = -1;
	struct ovdk_action actions[MAX_ACTIONS] = {{0}};
	uint8_t num_actions = 0;
	uint8_t num_pkts = 0;

	/* Invalid params */

	/* Invalid dpif */
	result = dpif_p->dpif_class->execute(NULL, &execute);
	assert(result == EINVAL);

	/* Invalid execute */
	result = dpif_p->dpif_class->execute(dpif_p, NULL);
	assert(result == EINVAL);

	/* Invalid execute->packet */
	execute.packet = NULL;
	result = dpif_p->dpif_class->execute(dpif_p, &execute);
	assert(result == EINVAL);

	/* Normal case */
	/* Add fake entry to vport table - needed by dpif_dpdk_execute to determine pipeline */
	port_no = OVDK_VPORT_TYPE_PHY;
	result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_PHY, pipeline_id, "dummyname", &port_no);
	assert(result == 0);

	create_dpif_execute_message(&execute);
	result = dpif_p->dpif_class->execute(dpif_p, &execute);
	assert(result == 0);

	/* Check if execute was handled correctly */
	num_pkts = rte_ring_count(vswitchd_packet_ring[pipeline_id]);
	assert(num_pkts == 1);
	result = dequeue_packet_from_packet_ring(&actions[0], &num_actions, pipeline_id);
	assert(result == 0);
	assert(num_actions == 4);
	assert(actions[0].type == OVDK_ACTION_OUTPUT);
	assert(actions[0].data.output.port == DPDK_RING_STUB_ACTION_OUTPUT_0);
	assert(actions[1].type == OVDK_ACTION_OUTPUT);
	assert(actions[1].data.output.port == DPDK_RING_STUB_ACTION_OUTPUT_1);
	assert(actions[2].type == OVDK_ACTION_POP_VLAN);
	assert(actions[3].type == OVDK_ACTION_OUTPUT);
	assert(actions[3].data.output.port == DPDK_RING_STUB_ACTION_OUTPUT_2);

}

static const struct command commands[] = {
	{"port-add", 0, 0, test_dpif_dpdk_port_add},
	{"port-del", 0, 0, test_dpif_dpdk_port_del},
	{"get-stats", 0, 0, test_dpif_dpdk_get_stats},
	{"port-query-by-number", 0, 0, test_dpif_dpdk_port_query_by_number},
	{"port-query-by-name", 0, 0, test_dpif_dpdk_port_query_by_name},
	{"get-max-ports", 0, 0, test_dpif_dpdk_get_max_ports},
	{"port-dump-start", 0, 0, test_dpif_dpdk_port_dump_start},
	{"port-dump-next", 0, 0, test_dpif_dpdk_port_dump_next},
	{"port-dump-done", 0, 0, test_dpif_dpdk_port_dump_done},
	{"port-poll", 0, 0, test_dpif_dpdk_port_poll},
	{"port-poll-wait", 0, 0, test_dpif_dpdk_port_poll_wait},
	{"port-get-stats", 0, 0, test_dpif_dpdk_port_get_stats},
	{"recv", 0, 0, test_dpif_dpdk_recv},
	{"execute", 0, 0, test_dpif_dpdk_execute},
	{"flow_put", 0, 0, test_dpif_dpdk_flow_put},
	{"flow-del",          0, 0, test_dpif_dpdk_flow_del},
	{"flow-del-null-key", 0, 0, test_dpif_dpdk_flow_del__null_key},
	{"flow-del-null-dpif-flow-del", 0, 0, test_dpif_dpdk_flow_del__null_dpif_flow_del},
	{"flow-del-flow-entry-not-found",    0, 0, test_dpif_dpdk_flow_del__flow_entry_not_found},
	{"flow-get", 0, 0, test_dpif_dpdk_flow_get},
	{"flow-get-null-key", 0, 0, test_dpif_dpdk_flow_get__null_key},
	{"flow-get-flow-entry-not-found", 0, 0, test_dpif_dpdk_flow_get__flow_entry_not_found},
	{"flow-dump-start", 0, 0, test_dpif_dpdk_flow_dump_start},
	{"flow-dump-next", 0, 0, test_dpif_dpdk_flow_dump_next},
	{"flow-dump-next-null-state", 0, 0, test_dpif_dpdk_flow_dump_next__null_state},
	{"flow-dump-next-no-entry", 0, 0, test_dpif_dpdk_flow_dump_next__no_entry},
	{"flow-dump-done", 0, 0, test_dpif_dpdk_flow_dump_done},
	{"flow-dump-done-null-state", 0, 0, test_dpif_dpdk_flow_dump_done__null_state},
	{"flow-flush", 0, 0, test_dpif_dpdk_flow_flush},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	int err = 0;

	set_program_name(argv[0]);
	vlog_set_levels(NULL, VLF_ANY_FACILITY, VLL_EMER);
	vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

	time_alarm(30);  /* Allow a long time for DPDK to start */

	/* Initialise system */
	rte_eal_init(argc, argv);
	/* Init rings and fake mempool with 1280 mbufs & populate pipeline_mask */
	init_test_rings(1280, &pipeline_mask);

	err = dpif_create_and_open("br0", "dpdk", &dpif_p);
	assert(err == 0);

	/* Assume only three EAL parameters */
	run_command(argc - 6, argv + 6, commands);

	/* Cleanup system */
	dpif_p->dpif_class->destroy(dpif_p);

	return 0;
}

