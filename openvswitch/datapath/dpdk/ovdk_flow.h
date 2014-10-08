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

#ifndef __OVDK_FLOW_H_
#define __OVDK_FLOW_H_

#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_pipeline.h>

#include "ovdk_flow_types.h"

#define TCP_FLAG_MASK                  (0x3F)
/* Calculates the IPv4 header size using the Internet Header Length field (IHL).
 * This field contains the number of 32-bit words in the header. */
#define IPV4_HEADER_SIZE(ipv4_hdr) \
            (((ipv4_hdr)->version_ihl & 0x0F) * sizeof(uint32_t))
#define TCP_HDR_FROM_PKT(pkt) (struct tcp_hdr*)\
               (rte_pktmbuf_mtod(pkt, unsigned char *) + \
               sizeof(struct ether_hdr) + \
               IPV4_HEADER_SIZE(rte_pktmbuf_mtod(pkt, struct ipv4_hdr*)))

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

int flow_keys_extract(struct rte_mbuf **pkts, uint32_t num_pkts,
                      uint64_t *pkts_mask, __attribute__((unused)) void *arg);

uint64_t ovs_flow_used_time(uint64_t curr_tsc, uint64_t flow_tsc);

#endif /* __OVDK_FLOW_H_ */
