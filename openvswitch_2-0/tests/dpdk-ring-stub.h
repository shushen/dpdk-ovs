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

#ifndef __DPDK_RING_STUB_H__
#define __DPDK_RING_STUB_H__

#include "flow.h"
#include "dpif.h"
#include "datapath/dpdk/ovdk_datapath_messages.h"
#include "datapath/dpdk/ovdk_action_types.h"

#define action_output_build(action_struct, vport)   do { \
                             (action_struct)->type = OVDK_ACTION_OUTPUT; \
                             (action_struct)->data.output.port = (vport);\
                         } while (0)

#define action_drop_build(action_struct)   do { \
                             (action_struct)->type = OVDK_ACTION_NULL; \
                         } while (0)

#define action_pop_vlan_build(action_struct)   do { \
                             (action_struct)->type = OVDK_ACTION_POP_VLAN; \
                         } while (0)

#define action_push_vlan_build(action_struct, tci_)   do { \
                             (action_struct)->type = OVDK_ACTION_PUSH_VLAN; \
                             (action_struct)->data.vlan.tci = tci_;\
                         } while (0)

#define action_null_build(action_struct)   do { \
                             (action_struct)->type = OVDK_ACTION_NULL; \
                         } while (0)

#define NUM_CORES 	RTE_MAX_LCORE

#define DPDK_RING_STUB_IN_PORT      0
#define DPDK_RING_STUB_NW_PROTO     IPPROTO_TCP
#define DPDK_RING_STUB_TP_SRC       12345
#define DPDK_RING_STUB_TP_DST       80
#define DPDK_RING_STUB_NW_SRC       0x0a010101
#define DPDK_RING_STUB_NW_DST       0x0a0101fe
#define DPDK_RING_STUB_VLAN_TCI     0
#define DPDK_RING_STUB_DL_SRC_0     0
#define DPDK_RING_STUB_DL_SRC_1     1
#define DPDK_RING_STUB_DL_SRC_2     2
#define DPDK_RING_STUB_DL_SRC_3     3
#define DPDK_RING_STUB_DL_SRC_4     4
#define DPDK_RING_STUB_DL_SRC_5     5
#define DPDK_RING_STUB_DL_DST_0     5
#define DPDK_RING_STUB_DL_DST_1     4
#define DPDK_RING_STUB_DL_DST_2     3
#define DPDK_RING_STUB_DL_DST_3     2
#define DPDK_RING_STUB_DL_DST_4     1
#define DPDK_RING_STUB_DL_DST_5     0
#define DPDK_RING_STUB_DL_TYPE      ETHER_TYPE_IPv4
#define DPDK_RING_STUB_FLOW_HANDLE  0xdeadbeef

#define DPDK_RING_STUB_ACTION_OUTPUT_0    0
#define DPDK_RING_STUB_ACTION_OUTPUT_1    1
#define DPDK_RING_STUB_ACTION_OUTPUT_2    2

/* ring to receive messages from vswitchd */
extern struct rte_ring *vswitchd_request_ring[];
extern struct rte_ring *vswitchd_reply_ring[];
extern struct rte_ring *vswitchd_packet_ring[];

int enqueue_reply_on_reply_ring(struct ovdk_message reply, unsigned pipeline_id);
int enqueue_upcall_on_exception_ring(uint8_t upcall_cmd, unsigned pipeline_id);
int dequeue_packet_from_packet_ring(struct ovdk_action *actions, uint8_t *num_actions, unsigned pipeline_id);
int dequeue_request_from_request_ring(struct ovdk_message **request, unsigned pipeline_id);
void init_test_rings(unsigned mempool_size, uint64_t *mask);
void create_dpif_flow_get_message(struct dpif_flow_put *get);
void create_dpdk_flow_get_reply(struct ovdk_message *reply);
void create_dpif_flow_put_message(struct dpif_flow_put *put);
void create_dpdk_flow_put_reply(struct ovdk_message *reply, int error);
void create_dpif_flow_del_message(struct dpif_flow_del *del);
void create_dpdk_flow_del_reply(struct ovdk_message *reply);
void create_dpdk_port_reply(struct ovdk_message *reply, int return_code, uint32_t flags);
void create_dpif_execute_message(struct dpif_execute *execute);

#endif /* __DPDK_RING_STUB_H__ */
