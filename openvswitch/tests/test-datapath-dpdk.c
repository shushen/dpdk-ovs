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

#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_ether.h>

#include <string.h>
#include <limits.h>
#include <linux/openvswitch.h>

#include "action.h"
#include "stats.h"
#include "flow.h"
#include "vport.h"
#include "ut.h"
#include "jobs.h"

#include <assert.h>

#define action_output_build(action_struct, vport)   do { \
                             (action_struct)->type = ACTION_OUTPUT; \
                             (action_struct)->data.output.port = (vport);\
                         } while (0)

#define action_drop_build(action_struct)   do { \
                             (action_struct)->type = ACTION_NULL; \
                         } while (0)

#define action_pop_vlan_build(action_struct)   do { \
                             (action_struct)->type = ACTION_POP_VLAN; \
                         } while (0)

#define action_push_vlan_build(action_struct, tci_)   do { \
                             (action_struct)->type = ACTION_PUSH_VLAN; \
                             (action_struct)->data.vlan.tci = tci_;\
                         } while (0)

#define action_null_build(action_struct)   do { \
                             (action_struct)->type = ACTION_NULL; \
                         } while (0)

/* Try to execute action with a client interface, which should succeed */
static void
test_action_execute_output(int argc, char *argv[])
{
	struct rte_mbuf buf_multiple[5];
	struct rte_mbuf *buf_p_multiple[5];
	struct action action_multiple[MAX_ACTIONS] = {0};
	int count = 0;
	uint8_t vportid = 3;

	buf_p_multiple[0] = &buf_multiple[0];
	buf_p_multiple[1] = &buf_multiple[1];
	buf_p_multiple[2] = &buf_multiple[2];
	buf_p_multiple[3] = &buf_multiple[3];
	buf_p_multiple[4] = &buf_multiple[4];

	/* client */
	vport_init();
	action_output_build(&action_multiple[0], vportid);
	action_null_build(&action_multiple[1]);
	action_execute(action_multiple, buf_multiple);
	count = receive_from_vport(vportid, buf_p_multiple);
	assert(count == 1);
	assert(buf_p_multiple[1] == &buf_multiple[1]);
}

/* Try to execute action with invalid parameters, which should fail
 * with -1 */
static void
test_action_execute_output__invalid_params(int argc, char *argv[])
{
	struct rte_mbuf buf_multiple[5];
	struct action action_multiple[MAX_ACTIONS] = {0};
	int ret = 0;

	/* check incorrect parameters */
	vport_init();
	action_output_build(&action_multiple[0], 17);
	action_null_build(&action_multiple[1]);
	ret = action_execute(NULL, &buf_multiple[1]);
	assert(ret < 0);
	ret = action_execute(action_multiple, NULL);
	assert(ret < 0);
}

/* Try to execute action with a corrupt output action, which should fail
 * with -1 */
static void
test_action_execute_output__corrupt_action(int argc, char *argv[])
{
	/* Nothing to do. */
}

/* Try to execute action with the drop action, which should succeed */
static void
test_action_execute_drop(int argc, char *argv[])
{
	struct rte_mbuf buf_free;
	struct rte_mbuf buf_drop;
	struct action action_multiple[MAX_ACTIONS] = {0};
	struct action action_drop[MAX_ACTIONS] = {0};

	stats_init();
	stats_vswitch_clear();

	/* TODO: Break this into multiple tests? */
	/* check that mbuf is freed on drop */
	assert(memcmp(&buf_free, &buf_drop, sizeof(buf_drop)) != 0);
	buf_drop.pkt.next = NULL; /* Required for rte_pktmbuf_free */
	memcpy(&buf_free, &buf_drop, sizeof(buf_drop));
	assert(memcmp(&buf_free, &buf_drop, sizeof(buf_drop)) == 0);
	action_drop_build(&action_drop[0]);
	action_null_build(&action_drop[1]);
	action_execute(action_drop, &buf_free);
	assert(memcmp(&buf_free, &buf_drop, sizeof(buf_drop)) != 0);
	/* check that vswitch rx drop stats are increased */
	stats_vswitch_clear();
	assert(stats_vswitch_rx_drop_get() == 0);
	action_drop_build(&action_drop[0]);
	action_null_build(&action_drop[1]);
	action_execute(action_drop, &buf_drop);
	assert(stats_vswitch_rx_drop_get() == 1);
}

/* Try to execute action with the pop vlan action, which should succeed */
static void
test_action_execute_pop_vlan(int argc, char *argv[])
{
	/* We write some data into the place where a VLAN tag would
	 * be and the 4 bytes after. We then call action execute
	 * and make sure the fake VLAN tag is gone and has been replaced
	 * by the data in the 4 bytes after
	 */
	struct rte_mempool *pktmbuf_pool;
	struct action action_multiple[MAX_ACTIONS] = {0};

	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
                    20, /* num mbufs */
                    2048 + sizeof(struct rte_mbuf) + 128, /*pktmbuf size */
                    32, /*cache size */
                    sizeof(struct rte_pktmbuf_pool_private),
                    rte_pktmbuf_pool_init,
                    NULL, rte_pktmbuf_init, NULL, 0, 0);

	struct rte_mbuf *vlan_buf = rte_pktmbuf_alloc(pktmbuf_pool);

	vport_init();

	/* We have no real packet but the function which pops the VLAN does
	 * some checks of pkt len so we define a fake one here
	 */
	vlan_buf->pkt.pkt_len = 20;
	action_pop_vlan_build(&action_multiple[0]);
	action_null_build(&action_multiple[1]);
	int *pktmbuf_data = rte_pktmbuf_mtod(vlan_buf, int *);
	*(pktmbuf_data + 2) = 0xCAFED00D;
	/* Note last 2 bytes must be 0081, ie 8100 in network format */
	*(pktmbuf_data + 3) = 0x00000081; /* 12 bytes after src/dst MAC is vlan */
	*(pktmbuf_data + 4) = 0xBABEFACE;
	action_execute(action_multiple, vlan_buf);
	pktmbuf_data = rte_pktmbuf_mtod(vlan_buf, int *);
	assert(*(pktmbuf_data + 3) != 0x00000081);
	assert(*(pktmbuf_data + 3) == 0xBABEFACE);
	rte_pktmbuf_free(vlan_buf);
}

/* Modify packet ethernet header */
static void
test_action_execute_set_ethernet(int argc, char *argv[])
{
	struct rte_mempool *pktmbuf_pool;
	struct action action_multiple[MAX_ACTIONS] = {0};

	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
                    20, /* num mbufs */
                    2048 + sizeof(struct rte_mbuf) + 128, /*pktmbuf size */
                    32, /*cache size */
                    sizeof(struct rte_pktmbuf_pool_private),
                    rte_pktmbuf_pool_init,
                    NULL, rte_pktmbuf_init, NULL, 0, 0);

	struct rte_mbuf *ethernet_buf = rte_pktmbuf_alloc(pktmbuf_pool);

	struct ovs_key_ethernet set_ethernet;
	__u8 eth_src_set[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
	__u8 eth_dst_set[6] = {0xCA, 0xFE, 0xDE, 0xAD, 0xBE, 0xEF};
	memcpy(&set_ethernet.eth_src, &eth_src_set, sizeof(eth_src_set));
	memcpy(&set_ethernet.eth_dst, &eth_dst_set, sizeof(eth_dst_set));

	struct ovs_key_ethernet ethernet_orig;
	__u8 eth_src_orig[6] = {0xFF, 0xFF, 0xFF, 0xCC, 0xCC, 0xCC};
	__u8 eth_dst_orig[6] = {0xAA, 0xAA, 0xAA, 0xEE, 0xEE, 0xEE};
	memcpy(&ethernet_orig.eth_src, &eth_src_orig, sizeof(eth_src_orig));
	memcpy(&ethernet_orig.eth_dst, &eth_dst_orig, sizeof(eth_dst_orig));

	vport_init();
	action_multiple[0].type = ACTION_SET_ETHERNET;
	action_multiple[0].data.ethernet = set_ethernet;
	action_null_build(&action_multiple[1]);

	struct ovs_key_ethernet *pktmbuf_data =
		rte_pktmbuf_mtod(ethernet_buf, struct ovs_key_ethernet *);
	memcpy(pktmbuf_data, &ethernet_orig, sizeof(ethernet_orig));

	action_execute(action_multiple, ethernet_buf);
	pktmbuf_data = rte_pktmbuf_mtod(ethernet_buf, struct ovs_key_ethernet *);
	/* Can't compare struct directly as ovs_key_ethernet has src first then
	 * dst whereas the real ethernet header has dst first then source
	 */
	assert(memcmp(pktmbuf_data, &set_ethernet.eth_dst, sizeof(eth_dst_set)) == 0);
	assert(memcmp((uint8_t *)pktmbuf_data + sizeof(eth_dst_set),
	              &set_ethernet.eth_src, sizeof(eth_src_set)) == 0);
	rte_pktmbuf_free(ethernet_buf);
}


/* Modify packet ipv4 header */
static void
test_action_execute_set_ipv4(int argc, char *argv[])
{
	struct rte_mempool *pktmbuf_pool;
	struct action action_multiple[MAX_ACTIONS] = {0};
	struct ipv4_hdr *pkt_ipv4;

	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
                    20, /* num mbufs */
                    2048 + sizeof(struct rte_mbuf) + 128, /*pktmbuf size */
                    32, /*cache size */
                    sizeof(struct rte_pktmbuf_pool_private),
                    rte_pktmbuf_pool_init,
                    NULL, rte_pktmbuf_init, NULL, 0, 0);

	struct rte_mbuf *ipv4_buf = rte_pktmbuf_alloc(pktmbuf_pool);

	struct ovs_key_ipv4 set_ipv4;
	set_ipv4.ipv4_tos = 0xFF;

	vport_init();
	action_multiple[0].type = ACTION_SET_IPV4;
	action_multiple[0].data.ipv4 = set_ipv4;
	action_null_build(&action_multiple[1]);

	uint8_t *pktmbuf_data =
	          rte_pktmbuf_mtod(ipv4_buf, uint8_t *);
	pktmbuf_data += sizeof(struct ether_hdr);
	pkt_ipv4 = (struct ipv4_hdr *)(pktmbuf_data);
	pkt_ipv4->type_of_service = 0xaa;

	action_execute(action_multiple, ipv4_buf);
	pktmbuf_data = rte_pktmbuf_mtod(ipv4_buf, uint8_t *);
	pktmbuf_data += sizeof(struct ether_hdr);
	pkt_ipv4 = (struct ipv4_hdr *)(pktmbuf_data);

	assert(pkt_ipv4->type_of_service == set_ipv4.ipv4_tos);
	rte_pktmbuf_free(ipv4_buf);
}

/* Try to execute action with the push vlan (VID) action, which should
 * succeed */
static void
test_action_execute_push_vlan__vid(int argc, char *argv[])
{
	/* Write Ethertype value of 0x0800 to byte 11 of the packet,
	 * where it is expected, and assign a length to the packet.
	 * After call to action_execute:
	 * - the length of the packet should have increased by 4 bytes
	 * - the value of byte 11 should by 0x8100 (0081 in network format)
	 * - the value of byte 15 should by 0x0800 (0008 in network format)
	 * - the value of the TCI field should be equal to the assigned
	 *   value
	*/
	struct rte_mempool *pktmbuf_pool;
	struct action action_multiple[MAX_ACTIONS] = {0};

	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
                    20, /* num mbufs */
                    2048 + sizeof(struct rte_mbuf) + 128, /*pktmbuf size */
                    32, /*cache size */
                    sizeof(struct rte_pktmbuf_pool_private),
                    rte_pktmbuf_pool_init,
                    NULL, rte_pktmbuf_init, NULL, 0, 0);

	struct rte_mbuf *buf = rte_pktmbuf_alloc(pktmbuf_pool);
	uint16_t vid = htons(0x0123); /* VID is the last 12 bits of the TCI */

	vport_init();

	/* Set the packet length - after the VLAN tag has been inserted,
	 * the value should increase by 4 bytes (i.e. the length of the tag)
	 */
	buf->pkt.pkt_len = 64;
	action_push_vlan_build(&action_multiple[0], vid);
	action_null_build(&action_multiple[1]);
	short *data = rte_pktmbuf_mtod(buf, short *);
	*(data + 6) = 0x0008; /* Set Ethertype to 0008, i.e. 0800 in network format */
	action_execute(action_multiple, buf);
	data = rte_pktmbuf_mtod(buf, short *);
	assert(*(data + 6) == 0x0081); /* 802.1Q Ethertype has been inserted */
	assert(*(data + 7) == 0x2301); /* TCI value has been inserted */
	assert(*(data + 8) == 0x0008); /* Ethertype has been shifted by 4 bytes */
	assert(buf->pkt.pkt_len == 68);/* Packet length has increased by 4 bytes */
	rte_pktmbuf_free(buf);
}

/* Try to execute action with the push vlan (PCP) action, which should
 * succeed */
static void
test_action_execute_push_vlan__pcp(int argc, char *argv[])
{
	/* Write Ethertype value of 0x0800 to byte 11 of the packet,
	 * where it is expected, and assign a length to the packet.
	 * After call to action_execute:
	 * - the length of the packet should have increased by 4 bytes
	 * - the value of byte 11 should by 0x8100 (0081 in network format)
	 * - the value of byte 15 should by 0x0800 (0008 in network format)
	 * - the value of the TCI field should be equal to the assigned
	 *   value
	*/
	struct rte_mempool *pktmbuf_pool;
	struct action action_multiple[MAX_ACTIONS] = {0};

	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
                    20, /* num mbufs */
                    2048 + sizeof(struct rte_mbuf) + 128, /*pktmbuf size */
                    32, /*cache size */
                    sizeof(struct rte_pktmbuf_pool_private),
                    rte_pktmbuf_pool_init,
                    NULL, rte_pktmbuf_init, NULL, 0, 0);

	struct rte_mbuf *pcp_buf = rte_pktmbuf_alloc(pktmbuf_pool);
	uint16_t pcp_tci = htons(0x2000); /* PCP is the upper 3 bits of the TCI */

	vport_init();

	/* Set the packet length - after the VLAN tag has been inserted,
	 * the value should increase by 4 bytes (i.e. the length of the tag)
	 */
	pcp_buf->pkt.pkt_len = 64;
	action_push_vlan_build(&action_multiple[0], pcp_tci);
	action_null_build(&action_multiple[1]);
	short *pkt_data = rte_pktmbuf_mtod(pcp_buf, short *);
	*(pkt_data + 6) = 0x0008; /* Set Ethertype to 0008, i.e. 0800 in network format */
	action_execute(action_multiple, pcp_buf);
	pkt_data = rte_pktmbuf_mtod(pcp_buf, short *);
	assert(*(pkt_data + 6) == 0x0081); /* 802.1Q Ethertype has been inserted */
	assert(*(pkt_data + 7) == 0x0020); /* TCI value has been inserted */
	assert(*(pkt_data + 8) == 0x0008); /* Ethertype has been shifted by 4 bytes */
	assert(pcp_buf->pkt.pkt_len == 68);/* Packet length has increased by 4 bytes */
	rte_pktmbuf_free(pcp_buf);
}

/* Try to execute action with three output actions, which should succeed */
static void
test_action_execute_multiple_actions__three_output(int argc, char *argv[])
{
	/* Three different output ports */
	/* We need to be able to clone mbufs which requires an
	 * alloc which requires a mempool
	 */
	struct rte_mempool *pktmbuf_pool;
	struct rte_mbuf *mbuf_output = NULL;
	struct action action_multiple[MAX_ACTIONS] = {0};
	int count = 0;

	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
                    20, /* num mbufs */
                    2048 + sizeof(struct rte_mbuf) + 128, /*pktmbuf size */
                    32, /*cache size */
                    sizeof(struct rte_pktmbuf_pool_private),
                    rte_pktmbuf_pool_init,
                    NULL, rte_pktmbuf_init, NULL, 0, 0);

	mbuf_output = rte_pktmbuf_alloc(pktmbuf_pool);

	vport_init();
	action_output_build(&action_multiple[0], 3);
	action_output_build(&action_multiple[1], 33);
	action_output_build(&action_multiple[2], 17);
	action_null_build(&action_multiple[3]);

	action_execute(action_multiple, mbuf_output);

	count = receive_from_vport(3, &mbuf_output);
	assert(count == 1);
	count = receive_from_vport(33, &mbuf_output);
	assert(count == 1);
	count = receive_from_vport(17, &mbuf_output);
	assert(count == 1);
}

/* Try to execute action with a pop vlan and output action, which should succeed */
static void
test_action_execute_multiple_actions__pop_vlan_and_output(int argc, char *argv[])
{
	/* We write some data into the place where a VLAN tag would be and the 4
	 * bytes after. We then call action execute and make sure the fake VLAN tag
	 * is gone and has been replaced by the data in the 4 bytes after
	 *
	 * We then output the packet to a port and make the same checks
	 */
	struct rte_mempool *pktmbuf_pool;
	struct action action_multiple[MAX_ACTIONS] = {0};
	int count = 0;

	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
                    20, /* num mbufs */
                    2048 + sizeof(struct rte_mbuf) + 128, /*pktmbuf size */
                    32, /*cache size */
                    sizeof(struct rte_pktmbuf_pool_private),
                    rte_pktmbuf_pool_init,
                    NULL, rte_pktmbuf_init, NULL, 0, 0);

	struct rte_mbuf *vlan_output_buf = rte_pktmbuf_alloc(pktmbuf_pool);

	vport_init();

	/* We have no real packet but the function which pops the VLAN does
	 * some checks of pkt len so we define a fake one here
	 */
	vlan_output_buf->pkt.pkt_len = 20;
	action_pop_vlan_build(&action_multiple[0]);
	action_output_build(&action_multiple[1], 17);
	action_null_build(&action_multiple[2]);
	int *pktmbuf_data = rte_pktmbuf_mtod(vlan_output_buf, int *);
	*(pktmbuf_data + 2) = 0xCAFED00D;
	/* Note last 2 bytes must be 0081, ie 8100 in network format */
	*(pktmbuf_data + 3) = 0x00000081; /* 12 bytes after src/dst MAC is vlan */
	*(pktmbuf_data + 4) = 0xBABEFACE;
	action_execute(action_multiple, vlan_output_buf);
	pktmbuf_data = rte_pktmbuf_mtod(vlan_output_buf, int *);
	assert(*(pktmbuf_data + 3) != 0x00000081);
	assert(*(pktmbuf_data + 3) == 0xBABEFACE);
	count = receive_from_vport(17, &vlan_output_buf);
	pktmbuf_data = rte_pktmbuf_mtod(vlan_output_buf, int *);
	assert(count == 1);
	assert(*(pktmbuf_data + 3) != 0x00000081);
	assert(*(pktmbuf_data + 3) == 0xBABEFACE);
}

/* Try to add a normal flow and duplicate flow, and add a flow with
 * incorrect parameters, which should succeed, fail with -1 and fail
 * with -1 respectively */
static void
test_flow_table_add_flow(int argc, char *argv[])
{
	struct flow_key key1 = {1};
	struct action action_multiple[MAX_ACTIONS] = {0};
	int ret = 0;

	flow_table_init();

	/* TODO: Break this into multiple tests? */
	action_output_build(&action_multiple[0], 1);
	action_output_build(&action_multiple[1], 2);
	action_null_build(&action_multiple[2]);
	ret = flow_table_add_flow(&key1, action_multiple);
	assert(ret >= 0);
	/* check no duplicates */
	ret = flow_table_add_flow(&key1, action_multiple);
	assert(ret < 0);
	/* check incorrect parameters */
	flow_table_del_flow(&key1);
	ret = flow_table_add_flow(NULL, action_multiple);
	assert(ret < 0);
	ret = flow_table_add_flow(&key1, NULL);
	assert(ret < 0);
}

/* Try to delete a normal flow and a non-existent flow, which should succeed
 * and fail with -1 respectively */
static void
test_flow_table_del_flow(int argc, char *argv[])
{
	struct flow_key key1 = {1};
	struct action action_multiple[MAX_ACTIONS] = {0};
	int ret = 0;

	flow_table_init();

	/* TODO: Break this into multiple tests? */
	action_output_build(&action_multiple[0], 1);
	action_output_build(&action_multiple[1], 2);
	action_null_build(&action_multiple[2]);
	flow_table_add_flow(&key1, action_multiple);
	ret = flow_table_del_flow(&key1);
	assert(ret >= 0);
	/* check no flow match */
	ret = flow_table_get_flow(&key1, NULL, NULL);
	assert(ret < 0);
	ret = flow_table_del_flow(&key1);
	assert(ret < 0);
}

/* Try to delete all flows, which should succeed */
static void
test_flow_table_del_all(int argc, char *argv[])
{
	struct flow_key key1 = {1};
	struct flow_key key2 = {2};
	struct flow_key key_check = {0};
	struct action action_multiple[MAX_ACTIONS] = {0};
	struct action action_check[MAX_ACTIONS] = {0};
	struct flow_stats stats_check = {0};
	int ret = 0;

	flow_table_init();

	action_output_build(&action_multiple[0], 1);
	action_output_build(&action_multiple[1], 2);
	action_null_build(&action_multiple[2]);
	/* Add two flowss to be deleted */
	flow_table_add_flow(&key1, action_multiple);
	ret = flow_table_get_flow(&key1, NULL, NULL);
	assert(ret >= 0);
	flow_table_add_flow(&key2, action_multiple);
	ret = flow_table_get_flow(&key2,  NULL, NULL);
	assert(ret >= 0);
	/* delete all flows and ensure flows are deleted */
	flow_table_del_all();
	ret = flow_table_get_first_flow(&key_check, action_check, &stats_check);
	assert(ret < 0);
}

/* Try to get a flow, which should succeed */
static void
test_flow_table_get_flow(int argc, char *argv[])
{
	struct flow_key key1 = {1};
	struct action action_multiple[MAX_ACTIONS] = {0};
	struct action action_check[MAX_ACTIONS] = {0};
	struct flow_stats stats_zero = {0};
	struct flow_stats stats_check = {0};
	int ret = 0;

	flow_table_init();

	action_output_build(&action_multiple[0], 1);
	action_output_build(&action_multiple[1], 2);
	action_null_build(&action_multiple[2]);
	flow_table_add_flow(&key1, action_multiple);
	ret = flow_table_get_flow(&key1, action_check, &stats_check);
	assert(ret >= 0);
	assert(memcmp(&action_multiple[0], action_check, sizeof(struct action)) == 0);
	assert(memcmp(&action_multiple[1], &action_check[1], sizeof(struct action)) == 0);
	assert(memcmp(&stats_zero, &stats_check , sizeof(struct flow_stats)) == 0 );
}

/* Try to modify a flow, which should succeed */
static void
test_flow_table_mod_flow(int argc, char *argv[])
{
	struct flow_key key1 = {1};
	struct action action_multiple[MAX_ACTIONS] = {0};
	struct action action_check[MAX_ACTIONS] = {0};
	struct flow_stats stats_zero = {0};
	struct flow_stats stats_check = {0};
	int ret = 0;

	flow_table_init();

	/* add a flow with 2 actions */
	action_output_build(&action_multiple[0], 2);
	action_output_build(&action_multiple[1], 1);
	action_null_build(&action_multiple[2]);
	flow_table_add_flow(&key1, action_multiple);
	action_output_build(&action_multiple[0], 1);
	action_null_build(&action_multiple[1]);
	/*modify it to only have 1 action */
	flow_table_mod_flow(&key1, action_multiple, true);

	ret = flow_table_get_flow(&key1, action_check, &stats_check);
	assert(ret >= 0);
	assert(memcmp(&action_multiple[0], action_check, sizeof(struct action)) == 0);
	/* check that our flow now only has one entry */
	assert(action_check[1].type == ACTION_NULL);
	assert(memcmp(&stats_zero, &stats_check , sizeof(struct flow_stats)) == 0 );
}

/* Try to get first flow, which should succeed */
static void
test_flow_table_get_first_flow(int argc, char *argv[])
{
	struct flow_key key1 = {1};
	struct flow_key key_check = {0};
	struct action action_multiple[MAX_ACTIONS] = {0};
	struct action action_check[MAX_ACTIONS] = {0};
	struct flow_stats stats_zero = {0};
	struct flow_stats stats_check = {0};
	int ret = 0;

	flow_table_init();

	flow_table_del_all();
	action_output_build(&action_multiple[0], 1);
	action_null_build(&action_multiple[1]);
	flow_table_add_flow(&key1, action_multiple);
	ret = flow_table_get_first_flow(&key_check, action_check, &stats_check);
	assert(ret >= 0);
	assert(memcmp(action_multiple, action_check, sizeof(struct action)) == 0);
	assert(memcmp(&stats_zero, &stats_check, sizeof(struct flow_stats)) == 0 );
	assert(memcmp(&key1, &key_check, sizeof(struct flow_key)) == 0 );
}

/* Try to get next flow, which should succeed */
static void
test_flow_table_get_next_flow(int argc, char *argv[])
{
	struct flow_key key1 = {1};
	struct flow_key key2 = {2};
	struct flow_key key_check = {0};
	struct action action_multiple[MAX_ACTIONS] = {0};
	struct action action_check[MAX_ACTIONS] = {0};
	struct flow_stats stats_zero = {0};
	struct flow_stats stats_check = {0};
	int ret = 0;

	flow_table_init();

	flow_table_del_all();
	action_output_build(&action_multiple[0], 1);
	action_null_build(&action_multiple[1]);
	ret = flow_table_add_flow(&key1, action_multiple);
	assert(ret >= 0);
	ret = flow_table_add_flow(&key2, action_multiple);
	assert(ret >= 0);

	ret = flow_table_get_first_flow(&key_check, action_check, &stats_check);
	assert(ret >= 0);
	if (memcmp(&key1, &key_check, sizeof(struct flow_key)) == 0 ) {
		ret = flow_table_get_next_flow(&key_check, &key_check, action_check, &stats_check);
		assert(ret >= 0);
		assert(memcmp(action_multiple, action_check, sizeof(struct action)) == 0);
		assert(memcmp(&stats_zero, &stats_check, sizeof(struct flow_stats)) == 0 );
		assert(memcmp(&key2, &key_check, sizeof(struct flow_key)) == 0 );
	} else if (memcmp(&key2, &key_check, sizeof(struct flow_key) == 0)) {
		ret = flow_table_get_next_flow(&key_check, &key_check, action_check, &stats_check);
		assert(ret >= 0);
		assert(memcmp(action_multiple, action_check, sizeof(struct action)) == 0);
		assert(memcmp(&stats_zero, &stats_check, sizeof(struct flow_stats)) == 0 );
		assert(memcmp(&key1, &key_check, sizeof(struct flow_key)) == 0 );
	} else {
		assert(1==0);
	}

}

/* Try to increment stats for all vport counters, which should
 * succeed */
static void
test_stats_vport_xxx_increment(int argc, char *argv[])
{
	int vportid = 0;

	stats_init();
	stats_vport_clear_all();

	for (vportid = 0; vportid < MAX_VPORTS; vportid++) {
		stats_vport_rx_increment(vportid, 23);
		stats_vport_rx_drop_increment(vportid, 23);
		stats_vport_tx_increment(vportid, 23);
		stats_vport_tx_drop_increment(vportid, 23);
		stats_vport_overrun_increment(vportid, 23);
		stats_vport_rx_increment(vportid, 19);
		stats_vport_rx_drop_increment(vportid, 19);
		stats_vport_tx_increment(vportid, 19);
		stats_vport_tx_drop_increment(vportid, 19);
		stats_vport_overrun_increment(vportid, 19);
	}
}

/* Try to get stats for all vport counters, which should succeed */
static void
test_stats_vport_xxx_get(int argc, char *argv[])
{
	int vportid = 0;

	stats_init();
	stats_vport_clear_all();

	/* increment stats so there's something to check for */
	for (vportid = 0; vportid < MAX_VPORTS; vportid++) {
		stats_vport_rx_increment(vportid, 23);
		stats_vport_rx_drop_increment(vportid, 23);
		stats_vport_tx_increment(vportid, 23);
		stats_vport_tx_drop_increment(vportid, 23);
		stats_vport_overrun_increment(vportid, 23);
		stats_vport_rx_increment(vportid, 19);
		stats_vport_rx_drop_increment(vportid, 19);
		stats_vport_tx_increment(vportid, 19);
		stats_vport_tx_drop_increment(vportid, 19);
		stats_vport_overrun_increment(vportid, 19);
	}

	for (vportid = 0; vportid < MAX_VPORTS; vportid++) {
		assert(stats_vport_rx_get(vportid) == 42);
		assert(stats_vport_rx_drop_get(vportid) == 42);
		assert(stats_vport_tx_get(vportid) == 42);
		assert(stats_vport_tx_drop_get(vportid) == 42);
		assert(stats_vport_overrun_get(vportid) == 42);
	}
}

/* Try to clear stats for all vport counters, which should succeed */
static void
test_stats_vport_xxx_clear(int argc, char *argv[])
{
	int vportid = 0;

	stats_init();
	stats_vport_clear_all();

	/* increment stats so there's something to clear */
	for (vportid = 0; vportid < MAX_VPORTS; vportid++) {
		stats_vport_rx_increment(vportid, 23);
		stats_vport_rx_drop_increment(vportid, 23);
		stats_vport_tx_increment(vportid, 23);
		stats_vport_tx_drop_increment(vportid, 23);
		stats_vport_overrun_increment(vportid, 23);
		stats_vport_rx_increment(vportid, 19);
		stats_vport_rx_drop_increment(vportid, 19);
		stats_vport_tx_increment(vportid, 19);
		stats_vport_tx_drop_increment(vportid, 19);
		stats_vport_overrun_increment(vportid, 19);
	}

	for (vportid = 0; vportid < MAX_VPORTS; vportid++) {
		stats_vport_clear(vportid);
		assert(stats_vport_rx_get(vportid) == 0);
		assert(stats_vport_rx_drop_get(vportid) == 0);
		assert(stats_vport_tx_get(vportid) == 0);
		assert(stats_vport_tx_drop_get(vportid) == 0);
		assert(stats_vport_overrun_get(vportid) == 0);
	}
}

/* Try to increment stats for all vswitch counters, which should
 * succeed */
static void
test_stats_vswitch_increment(int argc, char *argv[])
{
	stats_init();
	stats_vswitch_clear();

	stats_vswitch_rx_drop_increment(23);
	stats_vswitch_tx_drop_increment(23);
	stats_vswitch_rx_drop_increment(19);
	stats_vswitch_tx_drop_increment(19);
}

/* Try to get stats for all vswitch, which should succeed */
static void
test_stats_vswitch_get(int argc, char *argv[])
{
	stats_init();
	stats_vswitch_clear();

	/* increment stats so there's something to check */
	stats_vswitch_rx_drop_increment(23);
	stats_vswitch_tx_drop_increment(23);
	stats_vswitch_rx_drop_increment(19);
	stats_vswitch_tx_drop_increment(19);

	assert(stats_vswitch_rx_drop_get() == 42);
	assert(stats_vswitch_tx_drop_get() == 42);
}

/* Try to get stats for all vswitch, which should succeed */
static void
test_stats_vswitch_clear(int argc, char *argv[])
{
	stats_init();
	stats_vswitch_clear();

	/* increment stats so there's something to check */
	stats_vswitch_rx_drop_increment(23);
	stats_vswitch_tx_drop_increment(23);
	stats_vswitch_rx_drop_increment(19);
	stats_vswitch_tx_drop_increment(19);

	stats_vswitch_clear();
	assert(stats_vswitch_rx_drop_get() == 0);
	assert(stats_vswitch_tx_drop_get() == 0);
}

static void
test_jobs_init(int argc, char *argv[])
{
	unsigned int i;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());

	jobs_init();
	RTE_LCORE_FOREACH(i) {
		assert(joblist_refs[i] != NULL);
		assert(joblist_refs[i]->nb_jobs == 0);
		assert(joblist_refs[i]->online == 0);
	}
	assert(MAXJOBS_PER_LCORE > 0);
}

static void
test_jobs_add_to_lcore(int argc, char *argv[])
{
	unsigned int i, j;
	int ret;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());

	jobs_init();
	RTE_LCORE_FOREACH(i) {
		assert(joblist_refs[i]->nb_jobs == 0);

		/* check if joblist can be filled up completely
		 * (function pointer is just random here but not NULL) */
		for (j = 0; j < MAXJOBS_PER_LCORE; ++j) {
			ret = jobs_add_to_lcore((lcore_job_t *)(uintptr_t)(j + i + 57),
			                        (void *)(uintptr_t)(i * j),
			                        i);
			assert(ret >= 0);
			assert(joblist_refs[i]->nb_jobs == (j + 1));
		}

		/* check if joblist is full now */
		ret = jobs_add_to_lcore((lcore_job_t *)(uintptr_t) 437, NULL, i);
		assert(ret < 0);
	}

	RTE_LCORE_FOREACH(i) {
		/* check if list fillup happened correctly */
		assert(joblist_refs[i]->nb_jobs == MAXJOBS_PER_LCORE);
		for (j = 0; j < MAXJOBS_PER_LCORE; ++j) {
			assert(joblist_refs[i]->jobs[j].func == (lcore_job_t *)(uintptr_t)(j + i) + 57);
			assert(joblist_refs[i]->jobs[j].arg  == (void *)(uintptr_t)(j * i));
		}
	}
}

static void
test_jobs_clear_lcore(int argc, char *argv[])
{
	unsigned int i, j;
	int ret;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());

	jobs_init();
	RTE_LCORE_FOREACH(i) {
		/* add some jobs so that we have something to clear */
		jobs_add_to_lcore((lcore_job_t *) 715, NULL, i);
		jobs_add_to_lcore((lcore_job_t *) 386, NULL, i);
		jobs_add_to_lcore((lcore_job_t *) 942, NULL, i);
	}

	RTE_LCORE_FOREACH(i) {
		assert(joblist_refs[i]->nb_jobs != 0);
		jobs_clear_lcore(i);
		assert(joblist_refs[i]->nb_jobs == 0);
	}
}

/* Test Job: Increments a test variable. In order to avoid arithmetic
 *           overflows, this test job increments a test variable until
 *           4096 is reached */
static void
_test_jobs_inc4096(void *argp)
{
	unsigned int *testvar = argp;

	if (*testvar < 4096)
		++*testvar;
}

static void
test_jobs_launch_master(int argc, char *argv[])
{
	volatile unsigned int testvar0 = 0;
	volatile unsigned int testvar1 = 0;
	unsigned int i;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());
	/* this test has to add three jobs */
	assert(MAXJOBS_PER_LCORE >= 3);

	jobs_init();
	jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar0, rte_lcore_id());
	jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar0, rte_lcore_id());
	jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar1, rte_lcore_id());

	for (i = 0; i < 657; ++i)
		jobs_run_master_lcore();

	assert(testvar0 == 438);
	assert(testvar1 == 219);
}

static void
test_jobs_launch_slave(int argc, char *argv[])
{
	volatile unsigned int testvar0 = 0;
	volatile unsigned int testvar1 = 0;
	enum rte_lcore_state_t slave_state;
	unsigned int slave_id;
	int ret;

	/* this test has to be executed on the master lcore */
	assert(rte_lcore_id() == rte_get_master_lcore());
	/* this test has to add two jobs */
	assert(MAXJOBS_PER_LCORE >= 2);
	/* we need at least one slave for this test */
	slave_id = rte_get_next_lcore(rte_lcore_id(), true, true);
	assert(slave_id < RTE_MAX_LCORE);

	jobs_init();
	jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar0, slave_id);
	jobs_add_to_lcore(_test_jobs_inc4096, (void *) &testvar1, slave_id);

	ret = jobs_launch_slave_lcore(slave_id);
	assert(ret >= 0);
	slave_state = rte_eal_get_lcore_state(slave_id);
	assert(slave_state == RUNNING);

	/* wait for a while */
	sleep(5);

	ret = jobs_stop_slave_lcore(slave_id);
	assert(ret == 0);
	slave_state = rte_eal_get_lcore_state(slave_id);
	assert(slave_state != RUNNING);

	assert(testvar0 != 0);
	assert(testvar1 != 0);
}

static const struct command commands[] = {
	{"action_execute_output", 0, 0, test_action_execute_output},
	{"action_execute_output__invalid_params", 0, 0, test_action_execute_output__invalid_params},
	{"action_execute_output__corrupt_action", 0, 0, test_action_execute_output__corrupt_action},
	{"action_execute_drop", 0, 0, test_action_execute_drop},
	{"action_execute_pop_vlan", 0, 0, test_action_execute_pop_vlan},
	{"action_execute_set_ethernet", 0, 0, test_action_execute_set_ethernet},
	{"action_execute_set_ipv4", 0, 0, test_action_execute_set_ipv4},
	{"action_execute_push_vlan__vid", 0, 0, test_action_execute_push_vlan__vid},
	{"action_execute_push_vlan__pcp", 0, 0, test_action_execute_push_vlan__pcp},
	{"action_execute_multiple_actions__three_output", 0, 0, test_action_execute_multiple_actions__three_output},
	{"action_execute_multiple_actions__pop_vlan_and_output", 0, 0, test_action_execute_multiple_actions__pop_vlan_and_output},

	{"flow_table_add_flow", 0, 0, test_flow_table_add_flow},
	{"flow_table_del_flow", 0, 0, test_flow_table_del_flow},
	{"flow_table_del_all", 0, 0, test_flow_table_del_all},
	{"flow_table_get_flow", 0, 0, test_flow_table_get_flow},
	{"flow_table_mod_flow", 0, 0, test_flow_table_mod_flow},

	{"flow_table_get_first_flow", 0, 0, test_flow_table_get_first_flow},
	{"flow_table_get_next_flow", 0, 0, test_flow_table_get_next_flow},

	{"stats_vport_xxx_increment", 0, 0, test_stats_vport_xxx_increment},
	{"stats_vport_xxx_get", 0, 0, test_stats_vport_xxx_get},
	{"stats_vport_xxx_clear", 0, 0, test_stats_vport_xxx_clear},

	{"stats_vswitch_increment", 0, 0, test_stats_vswitch_increment},
	{"stats_vswitch_get", 0, 0, test_stats_vswitch_get},
	{"stats_vswitch_clear", 0, 0, test_stats_vswitch_clear},

	{"jobs_init", 0, 0, test_jobs_init},
	{"jobs_add_to_lcore", 0, 0, test_jobs_add_to_lcore},
	{"jobs_clear_lcore", 0, 0, test_jobs_clear_lcore},
	{"jobs_launch_master", 0, 0, test_jobs_launch_master},
	{"jobs_launch_slave", 0, 0, test_jobs_launch_slave},

	{NULL, 0, 0, NULL},
};

void main(int argc, char *argv[])
{
	/* init EAL, parsing EAL args */
	int count = 0;
	count = rte_eal_init(argc, argv);
	assert(count >= 0);

	/* skip the `--` separating EAL params from test params */
	count++;

	run_command(argc - count, argv + count, commands);

	exit(EXIT_SUCCESS);
}
