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

#ifndef __OVS_VPORT_H_
#define __OVS_VPORT_H_

/**
 * Lookup the vport struct memzone
 *
 * Does a rte_memzone_lookup(MZ_VPORT_INFO) and returns the rte_memzone.
 * Needs to be called before any other function otherwise they will fail.
 * In case of error returns NULL. In case of called twice it will return the
 * vport memzone again.
 */
const struct rte_memzone *ovs_vport_lookup_vport_info(void);

/**
 * Check if port_name is a valid client port name
 *
 * Returns 0 in case port_name is a valid and existent client port name.
 * Otherwise returns -1.
 * ovs_vport_lookup_vport() must have been called before.
 */
int ovs_vport_is_vport_client(const char *port_name);

/**
 * Lookup the RX queue of vport client named port_name.
 *
 * Returns the RX rte_ring of vport client named port_name in case this
 * is valid. Otherwise returns NULL.
 * ovs_vport_lookup_vport() must have been called before.
 */
struct rte_ring *ovs_vport_client_lookup_rx_q(const char *port_name);

/**
 * Lookup the TX queue of vport client named port_name.
 *
 * Returns the TX rte_ring of vport client named port_name in case this
 * is valid. Otherwise returns NULL.
 * ovs_vport_lookup_vport() must have been called before.
 */
struct rte_ring *ovs_vport_client_lookup_tx_q(const char *port_name);

/**
 * Lookup the FREE queue of vport client named port_name.
 *
 * Returns the FREE rte_ring of vport client named port_name in case this
 * is valid. Otherwise returns NULL.
 * ovs_vport_lookup_vport() must have been called before.
 */
struct rte_ring *ovs_vport_client_lookup_free_q(const char *port_name);

/**
 * Lookup the ALLOC queue of vport client named port_name.
 *
 * Returns the ALLOC rte_ring of vport client named port_name in case this
 * is valid. Otherwise returns NULL.
 * ovs_vport_lookup_vport() must have been called before.
 */
struct rte_ring *ovs_vport_client_lookup_alloc_q(const char *port_name);

/**
 * Check if port_name is a valid vport name.
 *
 * Returns 0 in case port_name is a valid vport port name following the
 * established port naming policies in vport.c. Otherwise returns -1.
 */
int ovs_vport_is_vport_name_valid(const char *port_name);

/**
 * Lookup the host mempool
 *
 * Returns the host mempool or a null pointer on failure.
 */
struct rte_mempool *ovs_vport_host_lookup_packet_mempool(void);
#endif  /* __OVS_VPORT_H_ */
