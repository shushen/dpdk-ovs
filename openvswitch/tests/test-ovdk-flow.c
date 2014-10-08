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

#include <assert.h>
#include <rte_config.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_memzone.h>
#include <rte_ether.h>
#include <rte_byteorder.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include "command-line.h"
#include "datapath/dpdk/ovdk_flow.h"
#include "datapath/dpdk/ovdk_flow_types.h"
#include "datapath/dpdk/ovdk_mempools.h"
#include "datapath/dpdk/ovdk_vport.h"
#include "datapath/dpdk/ovdk_vport_types.h"
#include "datapath/dpdk/ovdk_vport_info.h"
#include "datapath/dpdk/ovdk_virtio-net.h"
#include "datapath/dpdk/ovdk_stats.h"
#include "datapath/dpdk/ovdk_hash.h"

#define MBUF_CACHE_SIZE 512
#define MBUF_OVERHEAD (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define RX_MBUF_DATA_SIZE 2048
#define MBUF_SIZE (RX_MBUF_DATA_SIZE + MBUF_OVERHEAD)
#define ETHER_TYPE_VLAN_BS 0x81
#define ETHER_TYPE_IPv4_BS 0x8

void compare_keys(struct ovdk_flow_key *return_key, struct ovdk_flow_key *local_key);
void test_ovdk_flow_key_extract__vlan_udp(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_ovdk_flow_key_extract__ipv4(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_ovdk_flow_key_extract__icmp(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_ovdk_flow_key_extract__tcp(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_ovdk_flow_key_extract__noproto(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);

/* Ether - vlan - ipv4 - beoffset = 0 so uses OVS_FRAG_TYPE_NONE - IPPROTO_UDP - returns */
void
test_ovdk_flow_key_extract__vlan_udp(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint64_t unused = 4001;
	uint32_t num = 1;
	uint32_t local_sig = 0;
	uint32_t *return_sig = NULL;
	void *arg = NULL;
	unsigned char *loc = NULL;
	struct rte_mbuf *p = NULL;
	struct ether_addr src_addr = { .addr_bytes = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02} };
	struct ether_addr dst_addr = { .addr_bytes = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01} };
	struct ovdk_flow_key local_key;
	struct ovdk_flow_key *return_key = NULL;
	struct vlan_hdr *vlan_hdr = NULL;
	struct ether_hdr *ether_hdr = NULL;
	struct ipv4_hdr *ipv4_hdr = NULL;
	struct udp_hdr *udp = NULL;
	struct rte_mempool *mp = NULL;
	mp = rte_mempool_create(PKTMBUF_POOL_NAME, 1, MBUF_SIZE, MBUF_CACHE_SIZE,
	sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
	rte_pktmbuf_init, NULL, rte_socket_id(), 0);
	p = rte_pktmbuf_alloc(mp);
	assert( p != NULL);

	local_key.in_port = 0x1;
	local_key.ip_src = 0xDEADBEEF;
	local_key.ip_dst = 0xBABEFACE;
	local_key.ip_proto = IPPROTO_UDP;
	local_key.ether_dst = dst_addr;
	local_key.ether_src = src_addr;
	local_key.ether_type = ETHER_TYPE_VLAN_BS;
	local_key.ip_frag = 0;
	local_key.tran_src_port = 0x4000;
	local_key.tran_dst_port = 0x4001;
	local_key.vlan_id = 0xF00;
	local_key.vlan_prio = 0x0;

	loc = rte_pktmbuf_mtod(p, unsigned char *);

	ether_hdr = (struct ether_hdr *)loc;
	ether_hdr->d_addr = local_key.ether_dst;
	ether_hdr->s_addr = local_key.ether_src;
	ether_hdr->ether_type = local_key.ether_type;
	loc += sizeof(struct ether_hdr);

	vlan_hdr = (struct vlan_hdr *)(loc);
	vlan_hdr->vlan_tci = 0x000F;
	vlan_hdr->eth_proto = ETHER_TYPE_IPv4_BS;
	loc += sizeof(struct vlan_hdr);

	ipv4_hdr = (struct ipv4_hdr *)(loc);
	ipv4_hdr->next_proto_id = local_key.ip_proto;
	ipv4_hdr->fragment_offset = 0x0000;
	ipv4_hdr->dst_addr = local_key.ip_dst;
	ipv4_hdr->src_addr = local_key.ip_src;
	loc += IPV4_HEADER_SIZE(ipv4_hdr);

	udp = (struct udp_hdr *)(loc);
	udp->dst_port = local_key.tran_dst_port;
	udp->src_port = local_key.tran_src_port;

	local_key.ether_type = ETHER_TYPE_IPv4_BS;
	local_sig = test_hash(&local_key, sizeof(struct ovdk_flow_key), 0);

	arg = &local_key.in_port;
	ovdk_stats_init();
	flow_keys_extract(&p, num, &unused, arg);

	return_key = (void *)RTE_MBUF_METADATA_UINT8_PTR(p, 32);
	return_sig = (void *)RTE_MBUF_METADATA_UINT32_PTR(p, 0);
	compare_keys(return_key, &local_key);
	assert( *return_sig == local_sig);
}

void
test_ovdk_flow_key_extract__ipv4(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint64_t unused = 4001;
	uint32_t num = 1;
	void *arg = NULL;
	unsigned char *loc = NULL;
	struct rte_mbuf *p = NULL;
	struct ether_addr src_addr = { .addr_bytes = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02} };
	struct ether_addr dst_addr = { .addr_bytes = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01} };
	struct ovdk_flow_key local_key;
	struct ovdk_flow_key *return_key = NULL;
	struct ipv4_hdr *ipv4_hdr = NULL;
	struct ether_hdr *ether_hdr = NULL;
	struct rte_mempool *mp = NULL;
	mp = rte_mempool_create(PKTMBUF_POOL_NAME, 1, MBUF_SIZE, MBUF_CACHE_SIZE,
	sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
	rte_pktmbuf_init, NULL, rte_socket_id(), 0);
	p = rte_pktmbuf_alloc(mp);
	assert( p != NULL);

	local_key.in_port = 0x1;
	local_key.ip_src = 0xDEADBEEF;
	local_key.ip_dst = 0xFEEDBABE;
	local_key.ip_proto = IPPROTO_TCP;
	local_key.ether_dst = dst_addr;
	local_key.ether_src = src_addr;
	local_key.ether_type = ETHER_TYPE_IPv4_BS;
	local_key.ip_frag = 0x2;
	local_key.vlan_id = 0;
	local_key.vlan_prio = 0;
	local_key.tran_src_port = 0;
	local_key.tran_dst_port = 0;

	loc = rte_pktmbuf_mtod(p, unsigned char *);

	ether_hdr = (struct ether_hdr *)loc;
	ether_hdr->d_addr = local_key.ether_dst;
	ether_hdr->s_addr = local_key.ether_src;
	ether_hdr->ether_type = local_key.ether_type;
	loc += sizeof(struct ether_hdr);

	ipv4_hdr = (struct ipv4_hdr *)(loc);
	ipv4_hdr->next_proto_id = local_key.ip_proto;
	ipv4_hdr->fragment_offset = 0xFF1F;
	ipv4_hdr->dst_addr = local_key.ip_dst;
	ipv4_hdr->src_addr = local_key.ip_src;

	arg = &(local_key.in_port);
	ovdk_stats_init();
	flow_keys_extract(&p, num, &unused, arg);

	return_key = (struct ovdk_flow_key *)RTE_MBUF_METADATA_UINT8_PTR(p, 32);
	compare_keys(return_key, &local_key);
}

/* Ether - ipv4 - beoffset = 32 so matches HDR_MF_FLAG and uses OVS_FRAG_TYPE_FIRST - IPPROTO_ICMP returns */
void
test_ovdk_flow_key_extract__icmp(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint64_t unused = 4001;
	uint32_t num = 1;
	uint32_t local_sig = 0;
	uint32_t *return_sig = NULL;
	void *arg = NULL;
	unsigned char *loc = NULL;
	struct rte_mbuf *p = NULL;
	struct ether_addr src_addr = { .addr_bytes = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02} };
	struct ether_addr dst_addr = { .addr_bytes = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01} };
	struct ovdk_flow_key local_key;
	struct ovdk_flow_key *return_key = NULL;
	struct vlan_hdr *vlan_hdr = NULL;
	struct ether_hdr *ether_hdr = NULL;
	struct ipv4_hdr *ipv4_hdr = NULL;
	struct icmp_hdr *icmp = NULL;
	struct rte_mempool *mp = NULL;
	mp = rte_mempool_create(PKTMBUF_POOL_NAME, 1, MBUF_SIZE, MBUF_CACHE_SIZE,
	sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
	rte_pktmbuf_init, NULL, rte_socket_id(), 0);
	p = rte_pktmbuf_alloc(mp);
	assert( p != NULL);

	local_key.in_port = 0x1;
	local_key.ip_src = 0xDEADBEEF;
	local_key.ip_dst = 0xBABEFACE;
	local_key.ip_proto = IPPROTO_ICMP;
	local_key.ether_dst = dst_addr;
	local_key.ether_src = src_addr;
	local_key.ether_type = ETHER_TYPE_VLAN_BS;
	local_key.ip_frag = 0x1;
	local_key.tran_src_port = 0x40;
	local_key.tran_dst_port = 0x40;
	local_key.vlan_id = 0xF00;
	local_key.vlan_prio = 0x0;

	loc = rte_pktmbuf_mtod(p, unsigned char *);

	ether_hdr = (struct ether_hdr *)loc;
	ether_hdr->d_addr = local_key.ether_dst;
	ether_hdr->s_addr = local_key.ether_src;
	ether_hdr->ether_type = local_key.ether_type;
	loc += sizeof(struct ether_hdr);

	vlan_hdr = (struct vlan_hdr *)(loc);
	vlan_hdr->vlan_tci = 0x000F;
	vlan_hdr->eth_proto = ETHER_TYPE_IPv4_BS;
	loc += sizeof(struct vlan_hdr);

	ipv4_hdr = (struct ipv4_hdr *)(loc);
	ipv4_hdr->next_proto_id = local_key.ip_proto;
	ipv4_hdr->fragment_offset = 0x20;
	ipv4_hdr->dst_addr = local_key.ip_dst;
	ipv4_hdr->src_addr = local_key.ip_src;
	loc += IPV4_HEADER_SIZE(ipv4_hdr);

	icmp = (struct icmp_hdr *)(loc);
	icmp->icmp_code = local_key.tran_dst_port;
	icmp->icmp_type = local_key.tran_src_port;

	local_key.tran_dst_port = rte_cpu_to_be_16(local_key.tran_dst_port);
	local_key.tran_src_port = rte_cpu_to_be_16(local_key.tran_src_port);

	local_key.ether_type = ETHER_TYPE_IPv4_BS;
	local_sig = test_hash(&local_key, sizeof(struct ovdk_flow_key), 0);

	arg = &local_key.in_port;
	ovdk_stats_init();
	flow_keys_extract(&p, num, &unused, arg);

	return_key = (void *)RTE_MBUF_METADATA_UINT8_PTR(p, 32);
	return_sig = (void *)RTE_MBUF_METADATA_UINT32_PTR(p, 0);
	compare_keys(return_key, &local_key);
	assert( *return_sig == local_sig);
}

/* Ether - ipv4 - beoffset = 0 so uses OVS_FRAG_TYPE_NONE - default ip_proto - returns */
void
test_ovdk_flow_key_extract__noproto(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint64_t unused = 4001;
	uint32_t num = 1;
	uint32_t local_sig = 0;
	uint32_t *return_sig = NULL;
	void *arg = NULL;
	unsigned char *loc = NULL;
	struct rte_mbuf *p = NULL;
	struct ether_addr src_addr = { .addr_bytes = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02} };
	struct ether_addr dst_addr = { .addr_bytes = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01} };
	struct ovdk_flow_key local_key;
	struct ovdk_flow_key *return_key = NULL;
	struct ipv4_hdr *ipv4_hdr = NULL;
	struct ether_hdr *ether_hdr = NULL;
	struct rte_mempool *mp = NULL;
	mp = rte_mempool_create(PKTMBUF_POOL_NAME, 1, MBUF_SIZE, MBUF_CACHE_SIZE,
	sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
	rte_pktmbuf_init, NULL, rte_socket_id(), 0);
	p = rte_pktmbuf_alloc(mp);
	assert( p != NULL);

	local_key.in_port = 0x1;
	local_key.ip_src = 0xDEADBEEF;
	local_key.ip_dst = 0xFEEDBABE;
	local_key.ip_proto = 0xFF;
	local_key.ether_dst = dst_addr;
	local_key.ether_src = src_addr;
	local_key.ether_type = ETHER_TYPE_IPv4_BS;
	local_key.ip_frag = 0x0;
	local_key.tran_src_port = 0x0000;
	local_key.tran_dst_port = 0x0000;
	local_key.vlan_id = 0;
	local_key.vlan_prio = 0;

	local_sig = test_hash(&local_key, sizeof(struct ovdk_flow_key), 0);

	loc = rte_pktmbuf_mtod(p, unsigned char *);

	ether_hdr = (struct ether_hdr *)loc;
	ether_hdr->d_addr = local_key.ether_dst;
	ether_hdr->s_addr = local_key.ether_src;
	ether_hdr->ether_type = local_key.ether_type;
	loc += sizeof(struct ether_hdr);

	ipv4_hdr = (struct ipv4_hdr *)(loc);
	ipv4_hdr->next_proto_id = local_key.ip_proto;
	ipv4_hdr->fragment_offset = 0x0000;
	ipv4_hdr->dst_addr = local_key.ip_dst;
	ipv4_hdr->src_addr = local_key.ip_src;

	arg = &(local_key.in_port);
	ovdk_stats_init();
	flow_keys_extract(&p, num, &unused, arg);

	return_key = (struct ovdk_flow_key *)RTE_MBUF_METADATA_UINT8_PTR(p, 32);
	return_sig = (void *)RTE_MBUF_METADATA_UINT32_PTR(p, 0);
	compare_keys(return_key, &local_key);
	assert( *return_sig == local_sig);
}

/* Ether - vlan - ipv4 - beoffset = 0 so uses OVS_FRAG_TYPE_NONE - IPPROTO_TCP - returns */
void
test_ovdk_flow_key_extract__tcp(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint64_t unused = 4001;
	uint32_t num = 1;
	uint32_t local_sig = 0;
	uint32_t *return_sig = NULL;
	void *arg = NULL;
	unsigned char *loc = NULL;
	struct rte_mbuf *p = NULL;
	struct ether_addr src_addr = { .addr_bytes = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02} };
	struct ether_addr dst_addr = { .addr_bytes = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01} };
	struct ovdk_flow_key local_key;
	struct ovdk_flow_key *return_key = NULL;
	struct vlan_hdr *vlan_hdr = NULL;
	struct ether_hdr *ether_hdr = NULL;
	struct ipv4_hdr *ipv4_hdr = NULL;
	struct tcp_hdr *tcp = NULL;
	struct rte_mempool *mp = NULL;
	mp = rte_mempool_create(PKTMBUF_POOL_NAME, 1, MBUF_SIZE, MBUF_CACHE_SIZE,
	sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
	rte_pktmbuf_init, NULL, rte_socket_id(), 0);
	p = rte_pktmbuf_alloc(mp);
	assert( p != NULL);

	local_key.in_port = 0x1;
	local_key.ip_src = 0xDEADBEEF;
	local_key.ip_dst = 0xBABEFACE;
	local_key.ip_proto = IPPROTO_TCP;
	local_key.ether_dst = dst_addr;
	local_key.ether_src = src_addr;
	local_key.ether_type = ETHER_TYPE_VLAN_BS;
	local_key.ip_frag = 0;
	local_key.tran_src_port = 0x4000;
	local_key.tran_dst_port = 0x4001;
	local_key.vlan_id = 0xF00;
	local_key.vlan_prio = 0x0;

	loc = rte_pktmbuf_mtod(p, unsigned char *);

	ether_hdr = (struct ether_hdr *)loc;
	ether_hdr->d_addr = local_key.ether_dst;
	ether_hdr->s_addr = local_key.ether_src;
	ether_hdr->ether_type = local_key.ether_type;
	loc += sizeof(struct ether_hdr);

	vlan_hdr = (struct vlan_hdr *)(loc);
	vlan_hdr->vlan_tci = 0x000F;
	vlan_hdr->eth_proto = ETHER_TYPE_IPv4_BS;
	loc += sizeof(struct vlan_hdr);

	ipv4_hdr = (struct ipv4_hdr *)(loc);
	ipv4_hdr->next_proto_id = local_key.ip_proto;
	ipv4_hdr->fragment_offset = 0x0000;
	ipv4_hdr->dst_addr = local_key.ip_dst;
	ipv4_hdr->src_addr = local_key.ip_src;
	loc += IPV4_HEADER_SIZE(ipv4_hdr);

	tcp = (struct tcp_hdr *)(loc);
	tcp->dst_port = local_key.tran_dst_port;
	tcp->src_port = local_key.tran_src_port;

	arg = &local_key.in_port;
	ovdk_stats_init();
	flow_keys_extract(&p, num, &unused, arg);

	local_key.ether_type = ETHER_TYPE_IPv4_BS;
	local_sig = test_hash(&local_key, sizeof(struct ovdk_flow_key), 0);

	return_key = (void *)RTE_MBUF_METADATA_UINT8_PTR(p, 32);
	return_sig = (void *)RTE_MBUF_METADATA_UINT32_PTR(p, 0);
	compare_keys(return_key, &local_key);
	assert( *return_sig == local_sig);
}

void
compare_keys(struct ovdk_flow_key *return_key, struct ovdk_flow_key *local_key){
	assert( return_key->in_port == local_key->in_port );
	assert( return_key->ip_src == local_key->ip_src );
	assert( return_key->ip_dst == local_key->ip_dst );
	assert( return_key->ether_type == local_key->ether_type );
	assert( return_key->vlan_id == local_key->vlan_id );
	assert( return_key->vlan_prio == local_key->vlan_prio );
	assert( return_key->ether_dst.addr_bytes[0] == local_key->ether_dst.addr_bytes[0] );
	assert( return_key->ether_dst.addr_bytes[1] == local_key->ether_dst.addr_bytes[1] );
	assert( return_key->ether_dst.addr_bytes[2] == local_key->ether_dst.addr_bytes[2] );
	assert( return_key->ether_dst.addr_bytes[3] == local_key->ether_dst.addr_bytes[3] );
	assert( return_key->ether_dst.addr_bytes[4] == local_key->ether_dst.addr_bytes[4] );
	assert( return_key->ether_dst.addr_bytes[5] == local_key->ether_dst.addr_bytes[5] );
	assert( return_key->ether_src.addr_bytes[0] == local_key->ether_src.addr_bytes[0] );
	assert( return_key->ether_src.addr_bytes[1] == local_key->ether_src.addr_bytes[1] );
	assert( return_key->ether_src.addr_bytes[2] == local_key->ether_src.addr_bytes[2] );
	assert( return_key->ether_src.addr_bytes[3] == local_key->ether_src.addr_bytes[3] );
	assert( return_key->ether_src.addr_bytes[4] == local_key->ether_src.addr_bytes[4] );
	assert( return_key->ether_src.addr_bytes[5] == local_key->ether_src.addr_bytes[5] );
	assert( return_key->ip_frag == local_key->ip_frag );
	assert( return_key->ip_proto == local_key->ip_proto );
	assert( return_key->tran_src_port == local_key->tran_src_port );
	assert( return_key->tran_dst_port == local_key->tran_dst_port );
}

static const struct command commands[] = {
	{"key-extract-vlan", 0, 0, test_ovdk_flow_key_extract__vlan_udp},
	{"key-extract-ipv4", 0, 0, test_ovdk_flow_key_extract__ipv4},
	{"key-extract-icmp", 0, 0, test_ovdk_flow_key_extract__icmp},
	{"key-extract-tcp", 0, 0, test_ovdk_flow_key_extract__tcp},
	{"key-extract-noproto", 0, 0, test_ovdk_flow_key_extract__noproto},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	int eal_args = 0;
	eal_args = rte_eal_init(argc, argv);
	assert(eal_args > 0);
	eal_args += 1;

	run_command(argc - eal_args, argv + eal_args, commands);
	return 0;
}
