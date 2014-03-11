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

#ifndef __OVDK_DATAPATH_MESSAGES__
#define __OVDK_DATAPATH_MESSAGES__

#include "action.h"
#include "flow.h"
#include "stats.h"

/* A 'vport managment' message between vswitchd <-> datapath.  */
struct dpdk_vport_message {
	uint32_t id;                 /* Thread ID of sending thread */
	uint8_t cmd;                 /* Command to execute on vport. */
	uint32_t flags;              /* Additional flags, if any, or null. */
	uint32_t port_no;            /* Number of the vport. */
	char port_name[MAX_VPORT_NAME_SIZE];   /* Name of the vport. */
	enum vport_type type;        /* Type of the vport */
	char reserved[16];           /* Padding - currently reserved for future use */
	struct port_stats stats;     /* Current statistics for the given vport. */
};

struct dpdk_flow_message {
	uint32_t id;
	uint8_t cmd;
	uint32_t flags;
	struct flow_key key;
	struct flow_stats stats;
	struct action actions[MAX_ACTIONS];
	bool clear;
};

struct dpdk_packet_message {
	struct action actions[MAX_ACTIONS];
};

/* A message between vswitchd <-> datapath. */
struct dpdk_message {
	int16_t type;              /* Message type, if a request, or return code */
	union {                    /* Actual message */
		struct dpdk_vport_message vport_msg;
		struct dpdk_flow_message flow_msg;
		struct dpdk_packet_message packet_msg;
	};
};

struct dpdk_upcall {
	uint8_t cmd;         /* The reason why we are sending the packet to
	                        the daemon. */
	struct flow_key key; /* Extracted flow key for the packet. */
};

enum vport_cmd {
	VPORT_CMD_UNSPEC,
	VPORT_CMD_NEW,         /* Add new vport. */
	VPORT_CMD_DEL,         /* Delete existing vport. */
	VPORT_CMD_GET          /* Get stats for existing vport. */
};

enum flow_cmd {
	FLOW_CMD_UNSPEC,
	FLOW_CMD_NEW,          /* Add new flow. */
	FLOW_CMD_DEL,          /* Delete existing flow. */
	FLOW_CMD_GET           /* Get stats for existing flow. */
};

enum packet_cmd {
	PACKET_CMD_UNSPEC,
	PACKET_CMD_MISS,
	PACKET_CMD_ACTION,
	PACKET_CMD_EXECUTE
};

#endif /* __OVDK_DATAPATH_MESSAGES_H__ */
