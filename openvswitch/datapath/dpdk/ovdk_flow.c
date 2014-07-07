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

#include <rte_config.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_byteorder.h>

#include "linux/openvswitch.h"
#include "ovdk_hash.h"
#include "ovdk_flow.h"
#include "ovdk_stats.h"
#include "ovdk_vport.h"
#include "ovdk_pipeline.h"

#define VLAN_ID_MASK            0xFFF
#define VLAN_PRIO_SHIFT         13
#define ETHER_TYPE_IPv4_BS      0x8
#define ETHER_TYPE_VLAN_BS      0x81
#define PREFETCH_OFFSET         3

struct icmp_hdr {
	uint8_t icmp_type;
	uint8_t icmp_code;
	uint16_t icmp_csum;
	union {
		struct {
			uint16_t id;
			uint16_t seq;
		} echo;
		struct {
			uint16_t empty;
			uint16_t mtu;
		} frag;
		uint32_t gateway;
	} icmp_fields;
	uint8_t icmp_data[0];
};

static void flow_key_extract(uint32_t in_port, struct rte_mbuf *pkt);

int
flow_keys_extract(struct rte_mbuf **pkts, uint32_t num_pkts,
                  uint64_t *pkts_mask __attribute__((unused)) ,void *arg)
{
	int i = 0;
	int nb_rx = num_pkts;
	uint32_t vport_id = 0;

	for (i = 0; i < PREFETCH_OFFSET && i < nb_rx; i++) {
		rte_prefetch0(RTE_MBUF_METADATA_UINT32_PTR(pkts[i], 0));
		rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));
	}

	for (i = 0; i < nb_rx - PREFETCH_OFFSET; i++) {
		rte_prefetch0(RTE_MBUF_METADATA_UINT32_PTR(pkts[i + PREFETCH_OFFSET], 0));
		rte_prefetch0(rte_pktmbuf_mtod(pkts[i + PREFETCH_OFFSET], void *));
		flow_key_extract(*(uint32_t *)arg, pkts[i]);
	}

	for (; i < nb_rx; i++)
		flow_key_extract(*(uint32_t *)arg, pkts[i]);

	ovdk_vport_get_vportid(*(uint32_t *)arg, &vport_id);

	ovdk_stats_vport_rx_increment(vport_id, num_pkts);

	return 0;
}

/*
 * Extract 13 tuple from pkt as key
 */
static void
flow_key_extract(uint32_t in_port, struct rte_mbuf *pkt)
{
	uint32_t *signature = NULL;
	struct ovdk_flow_key *key = NULL;
	struct ether_hdr *ether_hdr = NULL;
	struct vlan_hdr *vlan_hdr = NULL;
	struct ipv4_hdr *ipv4_hdr = NULL;
	struct tcp_hdr *tcp = NULL;
	struct udp_hdr *udp = NULL;
	struct icmp_hdr *icmp = NULL;
	unsigned char *pkt_data = NULL;
	uint16_t vlan_tci = 0;
	uint16_t be_offset = 0;

	signature = (void *)RTE_MBUF_METADATA_UINT32_PTR(pkt, 0);
	key = (void *)RTE_MBUF_METADATA_UINT8_PTR(pkt, 32);

	key->in_port = in_port;

	/* Assume ethernet packet and get packet data */
	pkt_data = rte_pktmbuf_mtod(pkt, unsigned char *);
	ether_hdr = (struct ether_hdr *)pkt_data;
	pkt_data += sizeof(struct ether_hdr);

	key->ether_dst = ether_hdr->d_addr;
	key->ether_src = ether_hdr->s_addr;
	key->ether_type = ether_hdr->ether_type;

	if (key->ether_type == ETHER_TYPE_VLAN_BS) {
		vlan_hdr = (struct vlan_hdr *)pkt_data;
		pkt_data += sizeof(struct vlan_hdr);

		vlan_tci = rte_be_to_cpu_16(vlan_hdr->vlan_tci);
		key->vlan_id = vlan_tci & VLAN_ID_MASK;
		key->vlan_prio = vlan_tci >> VLAN_PRIO_SHIFT;

		key->ether_type = vlan_hdr->eth_proto;
	}

	if (key->ether_type == ETHER_TYPE_IPv4_BS) {
		ipv4_hdr = (struct ipv4_hdr *)pkt_data;
		pkt_data += IPV4_HEADER_SIZE(ipv4_hdr);

		key->ip_dst = ipv4_hdr->dst_addr;
		key->ip_src = ipv4_hdr->src_addr;
		key->ip_proto = ipv4_hdr->next_proto_id;
		//key->ip_tos = ipv4_hdr->type_of_service;
		//key->ip_ttl = ipv4_hdr->time_to_live;

		be_offset = ipv4_hdr->fragment_offset;
		if (be_offset & rte_be_to_cpu_16(IPV4_HDR_OFFSET_MASK)) {
			key->ip_frag = OVS_FRAG_TYPE_LATER;
			return;
		}
		if (be_offset & rte_be_to_cpu_16(IPV4_HDR_MF_FLAG))
			key->ip_frag = OVS_FRAG_TYPE_FIRST;
		else
			key->ip_frag = OVS_FRAG_TYPE_NONE;
	}

	switch (key->ip_proto) {
		case IPPROTO_TCP:
			tcp = (struct tcp_hdr *)pkt_data;

			key->tran_dst_port = tcp->dst_port;
			key->tran_src_port = tcp->src_port;
			break;
		case IPPROTO_UDP:
			udp = (struct udp_hdr *)pkt_data;

			key->tran_dst_port = udp->dst_port;
			key->tran_src_port = udp->src_port;
			break;
		case IPPROTO_ICMP:
			icmp = (struct icmp_hdr *)pkt_data;
			pkt_data += sizeof(struct icmp_hdr);

			key->tran_dst_port = icmp->icmp_code;
			key->tran_src_port = icmp->icmp_type;
			break;
		default:
			key->tran_dst_port = 0;
			key->tran_src_port = 0;
	}

	*signature = test_hash(key, sizeof(struct ovdk_flow_key), 0);
}

/*
 * Function translates TSC cycles to monotonic Linux time.
 */
uint64_t
ovs_flow_used_time(uint64_t curr_tsc, uint64_t flow_tsc)
{
	uint64_t curr_ms = 0;
	uint64_t idle_ms = 0;
	struct timespec tp = {0};

	/*
	 * Count idle time of flow. As TSC overflows infrequently
	 * (i.e. of the order of many years) and will only result
	 * in a spurious reading for flow used time, we don't check
	 * for overflow condition.
	 */
	idle_ms = (curr_tsc - flow_tsc) * 1000UL / cpu_freq;

	/* Return monotonic linux time */
	clock_gettime(CLOCK_MONOTONIC, &tp);
	curr_ms = tp.tv_sec * 1000UL + tp.tv_nsec / 1000000UL;

	return curr_ms - idle_ms;
}

