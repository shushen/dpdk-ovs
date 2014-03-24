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

#ifndef __VPORT_TYPES_H_
#define __VPORT_TYPES_H_

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <rte_mbuf.h>
#include <rte_memory.h>
#include <rte_memzone.h>

#define rpl_strnlen strnlen
#define PKTMBUF_POOL_NAME	"MProc_pktmbuf_pool"
#define MZ_VPORT_INFO		"OVS_vport_info"
#define MAX_VPORTS			256

struct vport_kni_fifo_names {
	char tx[RTE_MEMZONE_NAMESIZE];
	char rx[RTE_MEMZONE_NAMESIZE];
	char alloc[RTE_MEMZONE_NAMESIZE];
	char free[RTE_MEMZONE_NAMESIZE];
	char req[RTE_MEMZONE_NAMESIZE];
	char resp[RTE_MEMZONE_NAMESIZE];
	char sync[RTE_MEMZONE_NAMESIZE];
};

struct vport_client_ring_names {
	char rx[RTE_RING_NAMESIZE];
	char tx[RTE_RING_NAMESIZE];
	char free[RTE_RING_NAMESIZE];
};

enum vport_type {
	VPORT_TYPE_DISABLED = 0,
	VPORT_TYPE_VSWITCHD,
	VPORT_TYPE_BRIDGE,
	VPORT_TYPE_PHY,
	VPORT_TYPE_CLIENT,
	VPORT_TYPE_KNI,
	VPORT_TYPE_VETH,
	VPORT_TYPE_VHOST,
	VPORT_TYPE_MEMNIC,
};

struct vport_phy {
	struct rte_ring *tx_q;
	uint8_t index;
};

struct vport_client {
	struct vport_client_ring_names ring_names;
	struct rte_ring *rx_q;
	struct rte_ring *tx_q;
	struct rte_ring *free_q;
};

struct vport_kni {
	struct vport_kni_fifo_names fifo_names;
	uint8_t index;
};

struct vport_veth {
	uint8_t index;
};

struct vport_vhost {
	struct virtio_net *dev;
	uint8_t index;
};

struct vport_memnic {
	struct memnic_area *ptr;
	int up, down;
};

#define VPORT_INFO_NAMESZ	(32)

struct vport_info {
	enum vport_type __rte_cache_aligned type;
	char __rte_cache_aligned name[VPORT_INFO_NAMESZ];
	bool __rte_cache_aligned enabled;
	union {
		struct vport_phy phy;
		struct vport_client client;
		struct vport_kni kni;
		struct vport_veth veth;
		struct vport_vhost vhost;
		struct vport_memnic memnic;
	};
};

/*
 * Check if port_name is a vport valid name.
 *
 * In case port_name is a valid vport name (length is not greater than max vport
 * name length and all chars are alphanumeric) return 0. Otherwise returns -1.
 */
inline static int
vport_is_valid_name(const char *port_name)
{
	size_t length = 0;
	unsigned i = 0;

	length = strnlen(port_name, VPORT_INFO_NAMESZ + 1);

	/* If port_name length is equal or greater than VPORT_INFO_NAMESZ+1
	 * strlen will return VPORT_INFO_NAMESZ+1. In that case we know port_name
	 * is too big for a valid port name. strlen does have into account the
	 * final \0 byte but it does not return it as part of the string length. */
	if (length == VPORT_INFO_NAMESZ + 1)
		return -1;

	for (i = 0; i < length; i++)
		if (!isalnum(port_name[i]))
			return -1;

	return 0;
}

#endif /* __VPORT_TYPES_H_ */


