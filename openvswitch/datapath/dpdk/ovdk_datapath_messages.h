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

#ifndef __OVDK_DATAPATH_MESSAGES_H_
#define __OVDK_DATAPATH_MESSAGES_H_

/*
 * ovdk_datapath_messages
 *
 * ovdk_datapath_messages defines the messages that can be passed between
 * vswitchd and the dataplane.
 *
 * ovdk_datapath_messages provides enough information to allow the vswitchd
 * to create a request message and the dataplane to create a reply message.
 *
 * ovdk_datapath_messages defines the format of these messages and also the
 * names of the DPDK rings to enqueue and dequeue these messages.
 */

#include <stdbool.h>

#include "ovdk_config.h"
#include "ovdk_action_types.h"
#include "ovdk_flow_types.h"
#include "ovdk_stats_types.h"
#include "ovdk_vport_types.h"

#define OVDK_MAX_NAME_SIZE                   32
#define VSWITCHD_PACKET_RING_NAME            "OVDK%02u_Packet_Ring"
#define VSWITCHD_REPLY_RING_NAME             "OVDK%02u_Reply_Ring"
#define VSWITCHD_REQUEST_RING_NAME           "OVDK%02u_Request_Ring"
#define VSWITCHD_EXCEPTION_RING_NAME         "OVDK%02u_Exception_Ring"
#define VSWITCHD_PACKET_FREE_RING_NAME       "OVDK%02u_Packet_Free_Ring"
#define VSWITCHD_CONTROL_FREE_RING_NAME      "OVDK%02u_Control_Free_Ring"
#define VSWITCHD_PACKET_ALLOC_RING_NAME      "OVDK%02u_Packet_Alloc_Ring"
#define VSWITCHD_CONTROL_ALLOC_RING_NAME     "OVDK%02u_Control_Alloc_Ring"

#define OVDK_VPORT_CMD_FAMILY            0xE
#define OVDK_FLOW_CMD_FAMILY             0xF
#define OVDK_PACKET_CMD_FAMILY           0x1F

/* Flow messages flags bits */
#define FLAG_ROOT                        0x100
#define FLAG_MATCH                       0x200
#define FLAG_ATOMIC                      0x400
#define FLAG_DUMP                        (FLAG_ROOT | FLAG_MATCH)
#define FLAG_REPLACE                     0x100
#define FLAG_EXCL                        0x200
#define FLAG_CREATE                      0x400
#define FLAG_APPEND                      0x800

/* Vport messages flags bits */
#define VPORT_FLAG_OUT_PORT              0x001
#define VPORT_FLAG_IN_PORT               0x002
#define VPORT_FLAG_INOUT_PORT            (VPORT_FLAG_IN_PORT | VPORT_FLAG_OUT_PORT)

/*
 * A ovdk_vport_message is an ovdk_message type used to configure vports on the
 * dataplane.
 *
 * It is a control message that is sent via the request ring to ovdk_datapath.
 * After the datapath has handled the message, it should reply by sending a
 * message to the vswitchd.
 */
struct ovdk_vport_message {
	uint32_t thread_id;           /* Thread ID of sending thread */
	uint8_t cmd;                  /* Vport command to execute. */
	uint32_t flags;               /* Additional flags, if any, or null. */
	uint32_t vportid;             /* Number of the vport. */
	char port_name[OVDK_MAX_VPORT_NAMESIZE];
	                              /* Name of the vport. */
	enum ovdk_vport_type type;    /* Type of the vport */
	char reserved[16];            /* Padding - currently reserved for future use */
	struct ovdk_port_stats stats; /* Current statistics for the given vport. */
};

/*
 * A ovdk_flow_message is an ovdk_message type used to configure flows on the
 * dataplane
 *
 * It is a control message that is sent via the request ring to ovdk_datapath.
 * After the datapath has handled the message, it should reply by sending a
 * message to the vswitchd.
 */
struct ovdk_flow_message {
	uint32_t thread_id;          /* Thread ID of sending thread */
	uint8_t cmd;                 /* Flow command to execute */
	uint32_t flags;              /* Additional flags, if any, or null. */
	struct ovdk_flow_key key;    /* Flow key describing the flow */
	bool clear;                  /* Clear statistics */
	struct ovdk_action actions[OVDK_MAX_ACTIONS];
	                             /* A list of actions */
	uint8_t num_actions;         /* Number of actions in the list */
	struct ovdk_flow_stats stats;/* Flow statistics */
	uint64_t flow_handle;        /* A handle to the flow entry in the datapath */
};

/* An ovdk_packet_message is an ovdk_message type used to send packets from the
 * vswitchd to the dataplane.
 *
 * These messages are sent via the packet ring to ovdk_pipeline. The dataplane
 * is not required to send a reply to the vswitchd.
 */
struct ovdk_packet_message {
	struct ovdk_action actions[OVDK_MAX_ACTIONS];
	uint8_t num_actions;         /* Number of valid actions in actions */
};

/*
 * An ovdk_message is a message between vswitchd <-> dataplane.
 *
 * Control messages are sent to ovdk_datapath via the request ring. After
 * ovdk_datapath handles these control messages, it should send a reply via
 * the reply ring.
 *
 * Packet messages are sent to ovdk_pipeline via the packet ring.
 */
struct ovdk_message {
	int16_t type;           /* Message type */
	int16_t error;          /* Return code, if a reply */
	union {                 /* Actual message */
		struct ovdk_vport_message vport_msg;
		struct ovdk_flow_message flow_msg;
		struct ovdk_packet_message packet_msg;
	};
};

enum vport_cmd {
	VPORT_CMD_UNSPEC,
	VPORT_CMD_NEW,          /* Add new vport. */
	VPORT_CMD_DEL,          /* Delete existing vport. */
	VPORT_CMD_GET           /* Get stats for existing vport. */
};

enum flow_cmd {
	FLOW_CMD_UNSPEC,
	FLOW_CMD_NEW,           /* Add new flow. */
	FLOW_CMD_DEL,           /* Delete existing flow. */
	FLOW_CMD_GET            /* Get stats for existing flow. */
};

enum packet_cmd {
	PACKET_CMD_UNSPEC,
	PACKET_CMD_MISS,
	PACKET_CMD_ACTION,
	PACKET_CMD_EXECUTE
};

/*
 * An ovdk_upcall is a message between vswitchd and the dataplane.
 *
 * upcalls are sent to the vswitchd via the exception ring. An upcall is an
 * exception packet with some extra information 'struct ovdk_upcall' prepended
 * to the packet for use by the vswitchd.
 */
struct ovdk_upcall {
	uint8_t cmd;               /* Reason for sending packet to daemon. */
	struct ovdk_flow_key key;  /* Extracted flow key for the packet. */
};

#endif /* __OVDK_DATAPATH_MESSAGES_H_ */
