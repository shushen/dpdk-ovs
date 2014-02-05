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

#ifndef __VPORT_H_
#define __VPORT_H_

#include <stdint.h>
#include <rte_mbuf.h>

#include "kni.h"
#include "veth.h"
#include "vhost.h"

#define MAX_PHYPORTS        16
#define MAX_CLIENTS         16
#define MAX_VHOST_PORTS 	64
#define PKT_BURST_SIZE      32u
#define CLIENT0             0
#define CLIENT1             1
#define PHYPORT0            0x10
#define KNI0                0x20
#define VETH0               0x40
#define VHOST0              0x50
#define CLIENT_MASK         0x00
#define PORT_MASK           0x0F
#define KNI_MASK            0x1F
#define VETH_MASK           0x3F
#define VHOST_MASK          0x4F
#define VPORT_IN_USE		0
#define VPORT_NOT_IN_USE	1
#define VPORT_EXISTS		0

#define MAX_VPORTS			256

struct port_info {
	uint8_t num_phy_ports;
	uint8_t id[RTE_MAX_ETHPORTS];
};

struct port_stats {
	volatile uint64_t rx;        /* Rx packet count */
	volatile uint64_t tx;        /* Tx packet count */
	volatile uint64_t rx_bytes;  /* Tx bytes count */
	volatile uint64_t tx_bytes;  /* Tx bytes count */
	volatile uint64_t rx_drop;   /* Rx dropped packet count */
	volatile uint64_t tx_drop;   /* Tx dropped packet count */
	volatile uint64_t rx_error;  /* Rx error packet count */
	volatile uint64_t tx_error;  /* Tx error packet count */
};

struct port_info *ports;

struct virtio_net;
struct virtio_net_hdr_mrg_rxbuf;

/* Flags to communicate if a device can be removed safely from ovs_dpdk data path. */
#define REQUEST_DEV_REMOVAL 1
#define ACK_DEV_REMOVAL 0

volatile uint8_t dev_removal_flag[RTE_MAX_LCORE];   /* Flag to synchronize device removal. */

void vport_init(void);
void vport_fini(void);

int send_to_vport(uint32_t vportid, struct rte_mbuf *buf);
uint16_t receive_from_vport(uint32_t vportid, struct rte_mbuf **bufs);
void flush_nic_tx_ring(unsigned vportid);
const char *vport_name(unsigned vportid);
int16_t vport_in_use(unsigned vportid);
int vport_exists(unsigned vportid);
void vport_set_in_use(unsigned vportid);
void vport_set_not_in_use(unsigned vportid);
int vport_vhost_up(struct virtio_net *dev);
int vport_vhost_down(unsigned portid);

void flush_clients(void);
void flush_ports(void);
void flush_vhost_devs(void);

#endif /* __VPORT_H_ */


