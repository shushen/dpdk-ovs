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

#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdbool.h>

#include <rte_ether.h>
#include <rte_spinlock.h>

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NO_FLAGS 0
/*
 * Maximum number of clients, used for allocating space
 * for statistics
 */
#define MAX_CLIENTS             16
#define MAX_PHYPORTS            16
/* Maximum number of flow table entries */
#define MAX_FLOWS               64
/* define common names for structures shared between server and client */
#define MP_CLIENT_RXQ_NAME "MProc_Client_%u_RX"
#define MP_CLIENT_TXQ_NAME "MProc_Client_%u_TX"
#define PACKET_RING_NAME "MProc_Packet_RX"
#define MP_PORT_TXQ_NAME "MProc_PORT_%u_TX"
#define PKTMBUF_POOL_NAME "MProc_pktmbuf_pool"
#define MZ_PORT_INFO "MProc_port_info"
#define MZ_STATS_INFO "MProc_stats_info"
#define MZ_FLOW_TABLE "MProc_flow_table"

/* IP and Ethernet printing formats and arguments */
#define ETH_FMT "%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8
#define ETH_ARGS(ea) (ea)[0], (ea)[1], (ea)[2], (ea)[3], (ea)[4], (ea)[5]

#define IP_FMT "%"PRIu8".%"PRIu8".%"PRIu8".%"PRIu8
#define IP_ARGS(ip) ((ip >> 24) & 0xFF), ((ip >> 16) & 0xFF), ((ip >> 8) & 0xFF), (ip & 0xFF)

/*
 * This is the maximum number of digits that are required to represent
 * the largest possible unsigned int on a 64-bit machine. It will be used
 * to calculate the length of the strings above when %u is substituted.
 */
#define MAX_DIGITS_UNSIGNED_INT 20
#define MAX_VPORTS              48

/* Flow messages flags bits */
#define FLAG_ROOT              0x100
#define FLAG_MATCH             0x200
#define FLAG_ATOMIC            0x400
#define FLAG_DUMP              (FLAG_ROOT | FLAG_MATCH)

#define FLAG_REPLACE           0x100
#define FLAG_EXCL              0x200
#define FLAG_CREATE            0x400
#define FLAG_APPEND            0x800

#define FLOW_CMD_FAMILY		   0xF
#define PACKET_CMD_FAMILY	   0x1F

struct port_info {
	uint8_t num_ports;
	uint8_t id[RTE_MAX_ETHPORTS];
};

enum flow_cmd {
	FLOW_CMD_UNSPEC,
	FLOW_CMD_NEW,
	FLOW_CMD_DEL,
	FLOW_CMD_GET
};

enum packet_cmd {
	PACKET_CMD_UNSPEC,
	PACKET_CMD_MISS,
	PACKET_CMD_ACTION,
	PACKET_CMD_EXECUTE
};

struct flow_key {
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

struct flow_stats {
	rte_spinlock_t lock;	/* Lock for values below. */
	uint64_t packet_count;	/* Number of packets matched. */
	uint64_t byte_count;	/* Number of bytes matched. */
	uint64_t used;			/* Last used time (in hpet cycles). */
	uint8_t tcp_flags;		/* Union of seen TCP flags. */
};

struct dpdk_upcall {
	uint8_t cmd;			/* The reason why we are sending the packet to the daemon. */
	struct flow_key key;	/* Extracted flow key for the packet. */
};

struct dpdk_flow_message {
	uint8_t cmd;
	uint32_t flags;
	struct flow_key key;
	struct flow_stats stats;
	uint32_t action;
	bool clear;
};

struct dpdk_packet_message {
	uint32_t action;	/* What to do with the packet received from the daemon. */
};

struct dpdk_message {
	int16_t type;
	union {
		struct dpdk_flow_message flow_msg;
		struct dpdk_packet_message packet_msg;
	};
};

struct flow_table {
	volatile struct flow_key key[MAX_FLOWS];	/* Flow keys. */
	volatile uint32_t dst_port[MAX_FLOWS];		/* Flow actions (destination port). */
	volatile struct flow_stats stats[MAX_FLOWS];	/* Flow statistics. */
	volatile bool used[MAX_FLOWS];			/* Set flows. */
};

/*
 * Given the rx queue name template above, get the queue name
 */
static inline const char *
get_rx_queue_name(unsigned id)
{
	static char buffer[sizeof(MP_CLIENT_RXQ_NAME) + MAX_DIGITS_UNSIGNED_INT];

	rte_snprintf(buffer, sizeof(buffer), MP_CLIENT_RXQ_NAME, id);
	return buffer;
}

static inline const char *
get_tx_queue_name(unsigned id)
{
	static char buffer[sizeof(MP_CLIENT_TXQ_NAME) + MAX_DIGITS_UNSIGNED_INT];

	rte_snprintf(buffer, sizeof(buffer), MP_CLIENT_TXQ_NAME, id);
	return buffer;
}

static inline const char *
get_port_tx_queue_name(unsigned id)
{
	static char buffer[sizeof(MP_PORT_TXQ_NAME) + MAX_DIGITS_UNSIGNED_INT];

	rte_snprintf(buffer, sizeof(buffer), MP_PORT_TXQ_NAME, id);
	return buffer;
}

#endif
