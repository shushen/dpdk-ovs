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
#ifndef __OVDK_ACTION_TYPES_H_
#define __OVDK_ACTION_TYPES_H_

#include <stdint.h>

#include "linux/openvswitch.h"

/* Set of all supported actions */
enum ovdk_action_type {
	OVDK_ACTION_NULL,
	OVDK_ACTION_DROP,            /* Drop packet */
	OVDK_ACTION_OUTPUT,          /* Output packet to port */
	OVDK_ACTION_POP_VLAN,        /* Remove 802.1Q header */
	OVDK_ACTION_PUSH_VLAN,       /* Add 802.1Q VLAN header to packet */
	OVDK_ACTION_SET_TCP,         /* Modify TCP header */
	OVDK_ACTION_SET_UDP,         /* Modify UDP header */
	OVDK_ACTION_SET_IPV4,        /* Modify IPv4 header */
	OVDK_ACTION_SET_ETHERNET,    /* Modify Ethernet header */
	OVDK_ACTION_VSWITCHD,        /* Send packet to vswitchd */
	OVDK_ACTION_MAX              /* Maximum number of supported actions */
};

struct ovdk_action_output {
	uint32_t port;          /* Output port */
};

struct ovdk_action_drop {
};

struct ovdk_action_push_vlan {
	uint16_t tpid;          /* Tag Protocol ID (always 0x8100) */
	uint16_t tci;           /* Tag Control Information */
};

struct ovdk_action_vswitchd {
	uint32_t pid;           /* netlink pid */
};

struct ovdk_action {
	enum ovdk_action_type type;
	union {                 /* union of difference action types */
		struct ovdk_action_output output;
		struct ovdk_action_drop drop;
		struct ovdk_action_push_vlan vlan;
		struct ovs_key_ethernet ethernet;
		struct ovs_key_ipv4 ipv4;
		struct ovs_key_tcp tcp;
		struct ovs_key_udp udp;
		struct ovdk_action_vswitchd vswitchd;
		/* add other action structs here */
	} data;
};

#endif /* __OVDK_ACTION_TYPES_H_ */

