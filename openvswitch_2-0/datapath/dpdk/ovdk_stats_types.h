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

#ifndef __OVDK_STATS_TYPES_H_
#define __OVDK_STATS_TYPES_H_

#include <stdint.h>

/*
 * Flow statistics for a flow table entry in the dataplane
 * flow table.
 */
struct ovdk_flow_stats {
	uint64_t packet_count;  /* Number of packets matched. */
	uint64_t byte_count;    /* Number of bytes matched. */
	uint64_t used;          /* Last used time (in hpet cycles). */
	uint8_t tcp_flags;      /* Union of TCP flags "seen" during packet
	                         * traversal. */
} __attribute__((__packed__));

/*
 * Port statistics for a dataplane port. All statistics
 * are updated from the viewpoint of the dataplane. This means that
 * 'rx' refers to packets received on the port into the switch. 'tx'
 * refers to packets sent on the port from the switch.
 */
struct ovdk_port_stats {
	uint64_t rx;           /* Rx packet count */
	uint64_t tx;           /* Tx packet count */
	uint64_t rx_bytes;     /* Tx bytes count */
	uint64_t tx_bytes;     /* Tx bytes count */
	uint64_t rx_drop;      /* Rx dropped packet count */
	uint64_t tx_drop;      /* Tx dropped packet count */
	uint64_t rx_error;     /* Rx error packet count */
	uint64_t tx_error;     /* Tx error packet count */
};

#endif /* __OVDK_STATS_TYPES_H_ */
