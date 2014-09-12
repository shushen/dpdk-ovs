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

#ifndef __INCLUDE_RTE_PORT_VHOST_H__
#define __INCLUDE_RTE_PORT_VHOST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "rte_port.h"

/* Uncomment the line below to enable debug prints for userspace vhost devices. */
//#define VHOST_DEBUG

#ifdef VHOST_DEBUG
#define LOG_LEVEL RTE_LOG_DEBUG
#define LOG_DEBUG(log_type, fmt, args...) do {  \
        RTE_LOG(DEBUG, log_type, fmt, ##args);          \
} while (0)
#else
#define LOG_DEBUG(log_type, fmt, args...) do{} while(0)
#endif

/* Flags to communicate if a device can be removed safely from ovs_dpdk data path. */
#define REQUEST_DEV_REMOVAL 1
#define ACK_DEV_REMOVAL 0

/* Flag to synchronize device removal. */
volatile uint8_t pf_dev_removal_flag[RTE_MAX_LCORE];

struct rte_port_vhost_reader_params {
	struct virtio_net **dev;
	char pool_name[RTE_MEMPOOL_NAMESIZE];
};

extern struct rte_port_in_ops rte_port_vhost_reader_ops;

struct rte_port_vhost_writer_params {
	struct virtio_net **dev;
	uint32_t tx_burst_sz;
};

extern struct rte_port_out_ops rte_port_vhost_writer_ops;

#ifdef __cplusplus
}
#endif

#endif
