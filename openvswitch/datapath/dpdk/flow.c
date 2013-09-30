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

#include <rte_fbk_hash.h>
#include <rte_memzone.h>
#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_cpuflags.h>
#include <rte_tcp.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_byteorder.h>

#include "flow.h"
#include "action.h"

#define CHECK_POS(pos) do {\
                             if ((pos) >= MAX_FLOWS || (pos) < 0) return -1; \
                          } while (0)

#define CHECK_NULL(ptr)   do { \
                             if ((ptr) == NULL) return -1; \
                         } while (0)

#define CHECK_TYPE(type)   do { \
                             if ((type) == ACTION_NULL) return -1; \
                         } while (0)

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NO_FLAGS             0
#define HASH_NAME           "flow_table"
#define HASH_BUCKETS        4
#define SOCKET0             0
#define VLAN_ID_MASK        0xFFF
#define VLAN_PRIO_SHIFT     13
#define TCP_FLAG_MASK       0x3F
#define MZ_FLOW_TABLE       "MProc_flow_table"

/* IP and Ethernet printing formats and arguments */
#define ETH_FMT "%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8
#define ETH_ARGS(ea) (ea)[0], (ea)[1], (ea)[2], (ea)[3], (ea)[4], (ea)[5]
#define IP_FMT "%"PRIu8".%"PRIu8".%"PRIu8".%"PRIu8
#define IP_ARGS(ip) ((ip >> 24) & 0xFF), ((ip >> 16) & 0xFF), ((ip >> 8) & 0xFF), (ip & 0xFF)

struct flow_table_entry {
	struct flow_key key;      /* Flow key. */
	struct flow_stats stats; /* Flow statistics. */
	bool used;               /* Flow is used */
	enum action_type type;   /* Type of action */
	union {                  /* Union of different action types */
		struct action_output output;
	} action;
};

/* Parameters used for hash table in unit test functions. Name set later. */
static struct rte_hash_parameters ut_params = {
	.name               = HASH_NAME,
	.entries            = MAX_FLOWS,
	.bucket_entries     = HASH_BUCKETS,
	.key_len            = sizeof(struct flow_key),
	.hash_func          = rte_hash_crc,
	.hash_func_init_val = 0,
	.socket_id          = SOCKET0,
};

static struct flow_table_entry *flow_table[MAX_FLOWS];
static struct rte_hash *handle = NULL;

static uint64_t ovs_flow_used_time(uint64_t flow_tsc);
static void flow_key_print(volatile struct flow_key *key);

/* Initialize the flow table  */
void
flow_table_init(void)
{
	unsigned flow_table_size = sizeof(struct flow_table_entry) * MAX_FLOWS;
	const struct rte_memzone *mz = NULL;
	int pos = 0;
	/* set up array for flow table data */
	mz = rte_memzone_reserve(MZ_FLOW_TABLE, flow_table_size,
	                         rte_socket_id(), NO_FLAGS);
	if (mz == NULL)
		rte_exit(EXIT_FAILURE, "Cannot reserve memory zone for port "
		                       "information\n");
	memset(mz->addr, 0, flow_table_size);
	for (pos = 0; pos < MAX_FLOWS; pos++)
		flow_table[pos] = (struct flow_table_entry *)
		    ((char *)mz->addr + (sizeof(struct flow_table_entry) * pos));


	/* Check if hardware-accelerated hashing supported */
	if (ut_params.hash_func == rte_hash_crc &&
			!rte_cpu_get_flag_enabled(RTE_CPUFLAG_SSE4_2)) {
		RTE_LOG(WARNING, HASH, "CRC32 instruction requires SSE4.2, "
		              "which is not supported on this system. "
		              "Falling back to software hash.\n");
		ut_params.hash_func = rte_jhash;
	}

	handle = rte_hash_create(&ut_params);
	if (handle == NULL) {
		RTE_LOG(WARNING, APP, "Failed to create hash table\n");
		exit(EXIT_FAILURE);
	}
}

/*
 * Clear flow table statistics for 'key'
 */
int
flow_table_clear_stats(struct flow_key *key)
{
	int pos = 0;
	CHECK_NULL(key);
	pos = flow_table_lookup(key);
	CHECK_POS(pos);

	rte_spinlock_init((rte_spinlock_t *)&(flow_table[pos]->stats.lock));

	flow_table[pos]->stats.used = 0;
	flow_table[pos]->stats.tcp_flags = 0;
	flow_table[pos]->stats.packet_count = 0;
	flow_table[pos]->stats.byte_count = 0;

	return 0;
}

/*
 * Add 'key' to flow table
 */
int
flow_table_add_flow(struct flow_key *key, enum action_type type, void *action)
{
	int pos = 0;
	CHECK_NULL(key);
	CHECK_NULL(action);
	CHECK_TYPE(type);

	pos = flow_table_lookup(key);
	/* already exists */
	if (pos >= 0) {
		return -1;
	}

	pos = rte_hash_add_key(handle, key);
	CHECK_POS(pos);

	flow_table[pos]->key = *key;
	flow_table[pos]->type = type;
	flow_table[pos]->used = true;
	memcpy(&(flow_table[pos]->action), action,
	                 sizeof(flow_table[pos]->action));
	flow_table_clear_stats(key);
	return pos;
}

/*
 * Modify flow table entry referenced by 'key'. 'clear_stats' clears stastics
 * for that entry
 */
int
flow_table_mod_flow(struct flow_key *key, enum action_type type, void *action,
                    bool clear_stats)
{
	int rc = -1;
	int pos = 0;
	CHECK_NULL(key);
	CHECK_NULL(action);
	CHECK_TYPE(type);

	pos = flow_table_lookup(key);
	CHECK_POS(pos);

	if (flow_table[pos]->used) {
		if (action) {
			if (clear_stats) {
				flow_table_clear_stats(key);
			}
			memcpy(&(flow_table[pos]->action), action,
			                sizeof(flow_table[pos]->action));
			flow_table[pos]->type = type;
			rc = pos;
		}
	}

	return rc;
}

/*
 * Return flow entry from flow table using 'key' as index.
 *
 * All data is copied
 */
int flow_table_get_flow(struct flow_key *key, enum action_type *type,
                        void *action, struct flow_stats *stats)
{
	int pos = 0;
	int rc = -1;
	CHECK_NULL(key);
	pos = flow_table_lookup(key);
	CHECK_POS(pos);

	if (flow_table[pos]->used) {
		if (key) {
			*key = flow_table[pos]->key;
		}
		if (action) {
			memcpy(action, &flow_table[pos]->action,
			       sizeof(flow_table[pos]->action));
		}
		if (type) {
			*type = flow_table[pos]->type;
		}
		if (stats) {
			*stats = flow_table[pos]->stats;
		}
		rc = 0;
	}

	return rc;
}

/*
 * Will return the next flow entry after 'key' in 'next_key'
 *
 * All data is copied
 */
int flow_table_get_next_flow(struct flow_key *key,
              struct flow_key *next_key, enum action_type *type, void *action, struct flow_stats *stats)
{
	int pos = 0;
	int rc = -1;
	CHECK_NULL(key);

	pos = flow_table_lookup(key);
	CHECK_POS(pos);
	pos++;

	for (; pos < MAX_FLOWS; pos++) {
		if (flow_table[pos]->used) {
			if (next_key) {
				*next_key = flow_table[pos]->key;
			}
			if (action) {
				memcpy(action, &flow_table[pos]->action,
				       sizeof(flow_table[pos]->action));
			}
			if (type) {
				*type = flow_table[pos]->type;
			}
			if (stats) {
				*stats = flow_table[pos]->stats;
			}
			rc = 0;
			break;
		}

	}

	if (pos == MAX_FLOWS)
		rc = -1;

	return rc;
}

/*
 * Will return the first non-null flow entry in 'first_key'
 *
 * All data is copied
 */
int flow_table_get_first_flow(struct flow_key *first_key,
              enum action_type *type, void *action, struct flow_stats *stats)
{
	int pos = 0;
	int rc = -1;

	for (pos = 0; pos < MAX_FLOWS; pos++) {
		if (flow_table[pos]->used) {
			if (first_key) {
				*first_key = flow_table[pos]->key;
			}
			if (action) {
				memcpy(action, &flow_table[pos]->action,
			               sizeof(flow_table[pos]->action));
			}
			if (type) {
				*type = flow_table[pos]->type;
			}
			if (stats) {
				*stats = flow_table[pos]->stats;
			}
			rc = 0;
			break;
		}

	}

	if (pos == MAX_FLOWS)
		return -1;

	return rc;
}

/*
 * Delete flow table entry at 'key'
 */
int
flow_table_del_flow(struct flow_key *key)
{
	int pos = 0;
	CHECK_NULL(key);
	pos = rte_hash_del_key(handle, key);
	CHECK_POS(pos);

	memset((void *)&flow_table[pos]->key, 0,
	       sizeof(flow_table[pos]->action));
	memset((void *)&flow_table[pos]->action, 0,
	       sizeof(flow_table[pos]->action));
	flow_table_clear_stats(key);
	flow_table[pos]->type = ACTION_NULL;
	flow_table[pos]->used = false;
	return pos;
}

/*
 * Delete all flow table entries
 */
void
flow_table_del_all(void)
{
	int pos = 0;
	struct flow_key *key = NULL;

	for (pos = 0; pos < MAX_FLOWS; pos++) {
		key = &(flow_table[pos]->key);
		flow_table_clear_stats(key);
		rte_hash_del_key(handle, key);
		memset(key, 0, sizeof(struct flow_key));

		memset(key, 0, sizeof(flow_table[pos]->action));
		memset((void *)&flow_table[pos]->action, 0,
		       sizeof(flow_table[pos]->action));
		flow_table[pos]->type = ACTION_NULL;
		flow_table[pos]->used = false;
	}

}


/*
 * Use pkt to update stats at entry pos in flow_table
 */
int
flow_table_update_stats(struct flow_key *key, struct rte_mbuf *pkt)
{
	int pos = flow_table_lookup(key);
	CHECK_POS(pos);
	uint8_t tcp_flags = 0;

	if (flow_table[pos]->key.ether_type == ETHER_TYPE_IPv4 &&
	    flow_table[pos]->key.ip_proto == IPPROTO_TCP) {
		uint8_t *pkt_data = rte_pktmbuf_mtod(pkt, unsigned char *);
		pkt_data += sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr);
		struct tcp_hdr *tcp = (struct tcp_hdr *) pkt_data;
		tcp_flags = tcp->tcp_flags & TCP_FLAG_MASK;
	}

	rte_spinlock_lock((rte_spinlock_t *)&(flow_table[pos]->stats.lock));
	flow_table[pos]->stats.used = curr_tsc;
	flow_table[pos]->stats.packet_count++;
	flow_table[pos]->stats.byte_count += rte_pktmbuf_data_len(pkt);
	flow_table[pos]->stats.tcp_flags |= tcp_flags;
	rte_spinlock_unlock((rte_spinlock_t *)&(flow_table[pos]->stats.lock));

	return 0;
}

/*
 * Extract 13 tuple from pkt as key
 */
void
flow_key_extract(struct rte_mbuf *pkt, uint8_t in_port, struct flow_key *key)
{
	struct ether_hdr *ether_hdr = NULL;
	struct vlan_hdr *vlan_hdr = NULL;
	struct ipv4_hdr *ipv4_hdr = NULL;
	struct tcp_hdr *tcp = NULL;
	struct udp_hdr *udp = NULL;
	unsigned char *pkt_data = NULL;
	uint16_t next_proto = 0;
	uint16_t vlan_tci = 0;

	key->in_port = in_port;

	/* Assume ethernet packet and get packet data */
	pkt_data = rte_pktmbuf_mtod(pkt, unsigned char *);
	ether_hdr = (struct ether_hdr *)pkt_data;
	pkt_data += sizeof(struct ether_hdr);

	key->ether_dst = ether_hdr->d_addr;
	key->ether_src = ether_hdr->s_addr;
	key->ether_type = rte_be_to_cpu_16(ether_hdr->ether_type);

	next_proto = key->ether_type;
	if (next_proto == ETHER_TYPE_VLAN) {
		vlan_hdr = (struct vlan_hdr *)pkt_data;
		pkt_data += sizeof(struct vlan_hdr);

		vlan_tci = rte_be_to_cpu_16(vlan_hdr->vlan_tci);
		key->vlan_id = vlan_tci & VLAN_ID_MASK;
		key->vlan_prio = vlan_tci >> VLAN_PRIO_SHIFT;

		next_proto = rte_be_to_cpu_16(vlan_hdr->eth_proto);
		next_proto = key->ether_type;
	}

	if (next_proto == ETHER_TYPE_IPv4) {
		ipv4_hdr = (struct ipv4_hdr *)pkt_data;
		pkt_data += sizeof(struct ipv4_hdr);

		key->ip_dst = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
		key->ip_src = rte_be_to_cpu_32(ipv4_hdr->src_addr);
		key->ip_proto = ipv4_hdr->next_proto_id;
		key->ip_tos = ipv4_hdr->type_of_service;
		key->ip_ttl = ipv4_hdr->time_to_live;
	}

	switch (key->ip_proto) {
		case IPPROTO_TCP:
			tcp = (struct tcp_hdr *)pkt_data;
			pkt_data += sizeof(struct tcp_hdr);

			key->tran_dst_port = rte_be_to_cpu_16(tcp->dst_port);
			key->tran_src_port = rte_be_to_cpu_16(tcp->src_port);
			break;
		case IPPROTO_UDP:
			udp = (struct udp_hdr *)pkt_data;
			pkt_data += sizeof(struct udp_hdr);

			key->tran_dst_port = rte_be_to_cpu_16(udp->dst_port);
			key->tran_src_port = rte_be_to_cpu_16(udp->src_port);
			break;
		default:
			key->tran_dst_port = 0;
			key->tran_src_port = 0;
	}
}


/*
 * Lookup 'key' in hash table
 */
int flow_table_lookup(struct flow_key *key)
{
	return rte_hash_lookup(handle, key);
}

/*
 * Print flow key to screen
 */
static void
flow_key_print(volatile struct flow_key *key)
{
	printf("key.in_port = %"PRIu32"\n", key->in_port);
	printf("key.ether_dst = "ETH_FMT"\n", ETH_ARGS(key->ether_dst.addr_bytes));
	printf("key.ether_src = "ETH_FMT"\n", ETH_ARGS(key->ether_src.addr_bytes));
	printf("key.ether_type = %"PRIx16"\n", key->ether_type);
	printf("key.vlan_id = %"PRIu16"\n", key->vlan_id);
	printf("key.vlan_prio = %"PRIu8"\n", key->vlan_prio);
	printf("key.ip_src = "IP_FMT"\n", IP_ARGS(key->ip_src));
	printf("key.ip_dst = "IP_FMT"\n", IP_ARGS(key->ip_dst));
	printf("key.ip_proto = %"PRIu8"\n", key->ip_proto);
	printf("key.ip_tos  = %"PRIx8"\n", key->ip_tos);
	printf("key.ip_ttl = %"PRIu8"\n", key->ip_ttl);
	printf("key.tran_src_port  = %"PRIu16"\n", key->tran_src_port);
	printf("key.tran_dst_port  = %"PRIu16"\n", key->tran_dst_port);
}

/*
 * Function translates TSC cycles to monotonic linux time.
 */
static uint64_t
ovs_flow_used_time(uint64_t flow_tsc)
{
	uint64_t curr_ms = 0;
	uint64_t idle_ms = 0;
	struct timespec tp = {0};

	/*
	 * Count idle time of flow. As TSC overflows infrequently
	 * (i.e. of the order of many years) and will only result
	 * in a spurious reading for flow used time, we dont check
	 * for overflow condition
	 */
	idle_ms = (curr_tsc - flow_tsc) * 1000UL / cpu_freq;

	/* Return monotonic linux time */
	clock_gettime(CLOCK_MONOTONIC, &tp);
	curr_ms = tp.tv_sec * 1000UL + tp.tv_nsec / 1000000UL;

	return curr_ms - idle_ms;
}
