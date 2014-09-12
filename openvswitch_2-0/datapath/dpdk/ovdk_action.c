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

#include <stdio.h>

#include <rte_config.h>
#include <rte_ether.h>

#include "packets.h"
#include "ofpbuf.h"
#include "ofpbuf_helper.h"

#include "ovdk_config.h"
#include "ovdk_action.h"
#include "ovdk_action_types.h"

#define CHECK_NULL(ptr)   do { \
                             if ((ptr) == NULL) return -1; \
                         } while (0)
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

/*
 * Removes 802.1Q header from the packet associated with 'mbuf'
 */
inline void
ovdk_action_pop_vlan(struct rte_mbuf *mbuf)
{
	struct ofpbuf *ovs_pkt = NULL;

	ovs_pkt = overlay_ofpbuf(mbuf);
	eth_pop_vlan(ovs_pkt);
	update_mbuf(ovs_pkt, mbuf);
}

/*
 * Adds an 802.1Q header to the packet associated with 'mbuf'
 */
inline void
ovdk_action_push_vlan(const struct ovdk_action_push_vlan *action,
                             struct rte_mbuf *mbuf)
{
	struct ofpbuf *ovs_pkt = NULL;

	ovs_pkt = overlay_ofpbuf(mbuf);
	eth_push_vlan(ovs_pkt, action->tci);
	update_mbuf(ovs_pkt, mbuf);
}

/*
 * Modify the ethernet header as specified in the key
 */
inline void
ovdk_action_set_ethernet(const struct ovs_key_ethernet *ethernet_key,
                    struct rte_mbuf *mbuf)
{
	struct ether_hdr *ether_hdr;
	/* Note, there is no function in openvswitch.h to modify
	 * the ethernet addresses
	 */
	ether_hdr = mbuf->pkt.data;
	memcpy(&ether_hdr->d_addr, ethernet_key->eth_dst, sizeof(struct ether_addr));
	memcpy(&ether_hdr->s_addr, ethernet_key->eth_src, sizeof(struct ether_addr));
}

/*
 * Modify the IPV4 header as specified in the key
 */
inline void
ovdk_action_set_ipv4(const struct ovs_key_ipv4 *ipv4_key,
                    struct rte_mbuf *mbuf)
{
	struct ofpbuf *ovs_pkt = NULL;

	ovs_pkt = overlay_ofpbuf(mbuf);
	packet_set_ipv4(ovs_pkt, ipv4_key->ipv4_src, ipv4_key->ipv4_dst,
	                ipv4_key->ipv4_tos, ipv4_key->ipv4_ttl);
	update_mbuf(ovs_pkt, mbuf);
}

/*
 * Modify the TCP header as specified in the key
 */
inline void
ovdk_action_set_tcp(const struct ovs_key_tcp *tcp_key,
                    struct rte_mbuf *mbuf)
{
	struct ofpbuf *ovs_pkt = NULL;

	ovs_pkt = overlay_ofpbuf(mbuf);
	packet_set_tcp_port(ovs_pkt, tcp_key->tcp_src, tcp_key->tcp_dst);
	update_mbuf(ovs_pkt, mbuf);
}

/*
 * Modify the UDP header as specified in the key
 */
inline void
ovdk_action_set_udp(const struct ovs_key_udp *udp_key,
                    struct rte_mbuf *mbuf)
{
	struct ofpbuf *ovs_pkt = NULL;

	ovs_pkt = overlay_ofpbuf(mbuf);
	packet_set_udp_port(ovs_pkt, udp_key->udp_src, udp_key->udp_dst);
	update_mbuf(ovs_pkt, mbuf);

}
