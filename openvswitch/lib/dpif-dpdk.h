/*
 * Copyright 2012-2013 Intel Corporation All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DPIF_DPDK_H
#define DPIF_DPDK_H 1

#include <stdbool.h>

#include <rte_spinlock.h>
#include <rte_ether.h>

#include "ofpbuf.h"
#include "dpif.h"
#include "action.h"

#define DPIF_DPDK_FLOW_FAMILY	0xF
#define DPIF_DPDK_PACKET_FAMILY	0x1F

struct dpif_dpdk_flow_key {
	uint32_t in_port;
	struct ether_addr ether_dst;
	struct ether_addr ether_src;
	uint16_t ether_type;
	uint16_t vlan_id;
	uint8_t vlan_prio;
	uint32_t ip_src;
	uint32_t ip_dst;
	uint8_t ip_proto;
	uint8_t ip_tos;
	uint8_t ip_ttl;
	uint16_t tran_src_port;
	uint16_t tran_dst_port;
} __attribute__((__packed__));

struct dpif_dpdk_flow_stats {
	rte_spinlock_t lock;
	uint64_t packet_count;
	uint64_t byte_count;
	uint64_t used;
	uint8_t tcp_flags;
};

struct dpif_dpdk_upcall {
    uint8_t cmd;
    struct dpif_dpdk_flow_key key;
};

struct dpif_dpdk_flow_message {
	uint8_t cmd;
	uint32_t flags;
	struct dpif_dpdk_flow_key key;
	struct dpif_dpdk_flow_stats stats;
	struct action action;
	bool clear;
};

struct dpif_dpdk_packet_message {
    struct action action;
};

struct dpif_dpdk_message {
	int16_t type;
	union {
		struct dpif_dpdk_flow_message flow_msg;
		struct dpif_dpdk_packet_message packet_msg;
	};
};

struct dpif_dpdk_flow_state {
	struct dpif_dpdk_flow_message flow;
	struct dpif_flow_stats stats;
	struct ofpbuf actions_buf;
	struct ofpbuf key_buf;
};

#endif /* dpif-dpdk.h */
