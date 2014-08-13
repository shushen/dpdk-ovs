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

#include <rte_config.h>
#include <rte_string_fns.h>

#include "rte_port_ivshm.h"
#include "ovdk_vport_client.h"
#include "ovdk_stats.h"
#include "ovdk_mempools.h"

#define PORT_CLIENT_RX_BURST_SIZE      32
#define PORT_CLIENT_TX_BURST_SIZE      32
#define PORT_CLIENT_RX_RING_SIZE       2048
#define PORT_CLIENT_TX_RING_SIZE       2048
#define PORT_CLIENT_FREE_RING_SIZE     2048
#define PORT_CLIENT_ALLOC_RING_SIZE    128

#define PORT_CLIENT_RX_RING_NAME       "ovdk_ivshm_rx_ring_%u"
#define PORT_CLIENT_TX_RING_NAME       "ovdk_ivshm_tx_ring_%u"
#define PORT_CLIENT_FREE_RING_NAME     "ovdk_ivshm_free_ring_%u"
#define PORT_CLIENT_ALLOC_RING_NAME    "ovdk_ivshm_alloc_ring_%u"

/*
 * The size of this ring combined with the size of the other rings sharing it's
 * mempool should not exceed the size of that mempool. This is especially
 * important for this ring as it will be kept full.
 */

int
ovdk_vport_client_port_init(struct vport_info *vport_info)
{
	struct rte_pipeline_port_in_params *port_in_params = NULL;
	struct rte_pipeline_port_out_params *port_out_params = NULL;
	struct rte_port_ivshm_writer_params *port_writer_params = NULL;
	struct rte_port_ivshm_reader_params *port_reader_params = NULL;
	struct vport_client_ring_names *port_ring_names = NULL;
	struct rte_ring *ret = NULL;
	unsigned i = 0;

	/* Check for null vport info before use*/
	if(vport_info == NULL)
		rte_panic("Cannot init client port, invalid vport info\n");

	RTE_LOG(INFO,
	        USER1,
	        "Initializing Client port %u ...\n",
	        vport_info->vportid);

	port_in_params = &vport_info->port_in_params;

	/*port_out_params requires a loop, to save complexity it is below.*/

	port_reader_params = &vport_info->client.port_reader_client_params;
	port_writer_params = &vport_info->client.port_writer_client_params;
	port_ring_names = &vport_info->client.ring_names;

	vport_info->type = OVDK_VPORT_TYPE_CLIENT;

	port_writer_params->tx_burst_sz = PORT_CLIENT_TX_BURST_SIZE;

	snprintf(port_reader_params->mp,
	             RTE_MEMPOOL_NAMESIZE,
	             PKTMBUF_POOL_NAME);

	/* Init RX ring */
	snprintf(port_ring_names->rx,
	             RTE_RING_NAMESIZE,
	             PORT_CLIENT_RX_RING_NAME,
	             vport_info->vportid);

	snprintf(port_reader_params->rx_ring_name,
	             RTE_RING_NAMESIZE,
	             PORT_CLIENT_RX_RING_NAME,
	             vport_info->vportid);

	ret = rte_ring_create(
		port_reader_params->rx_ring_name,
		PORT_CLIENT_RX_RING_SIZE,
		rte_socket_id(),
		RING_F_SC_DEQ);

	if (ret == NULL) {
		rte_panic("Cannot create RX ring %u\n", vport_info->vportid);
	}

	ret = NULL;

	/* Init TX ring */
	snprintf(port_ring_names->tx,
	             RTE_RING_NAMESIZE,
	             PORT_CLIENT_TX_RING_NAME,
	             vport_info->vportid);

	snprintf(port_writer_params->tx_ring_name,
	             RTE_RING_NAMESIZE,
	             PORT_CLIENT_TX_RING_NAME,
	             vport_info->vportid);

	ret = rte_ring_create(
		port_writer_params->tx_ring_name,
		PORT_CLIENT_TX_RING_SIZE,
		rte_socket_id(),
		0);

	if (ret == NULL) {
		rte_panic("Cannot create TX ring for port %u\n",
		          vport_info->vportid);
	}

	ret = NULL;

	/* Init free ring */
	snprintf(port_ring_names->free,
	             RTE_RING_NAMESIZE,
	             PORT_CLIENT_FREE_RING_NAME,
	             vport_info->vportid);

	snprintf(port_reader_params->free_ring_name,
	             RTE_RING_NAMESIZE,
	             PORT_CLIENT_FREE_RING_NAME,
	             vport_info->vportid);

	ret = rte_ring_create(
		port_reader_params->free_ring_name,
		PORT_CLIENT_FREE_RING_SIZE,
		rte_socket_id(),
		RING_F_SC_DEQ);

	if (ret == NULL) {
		rte_panic("Cannot create free ring for port %u\n",
		          vport_info->vportid);
	}

	ret = NULL;

	/* Init alloc ring */
	snprintf(port_ring_names->alloc,
	             RTE_RING_NAMESIZE,
	             PORT_CLIENT_ALLOC_RING_NAME,
	             vport_info->vportid);

	snprintf(port_reader_params->alloc_ring_name,
	             RTE_RING_NAMESIZE,
	             PORT_CLIENT_ALLOC_RING_NAME,
	             vport_info->vportid);

	ret = rte_ring_create(
		port_reader_params->alloc_ring_name,
		PORT_CLIENT_ALLOC_RING_SIZE,
		rte_socket_id(),
		RING_F_SP_ENQ);

	if (ret == NULL) {
		rte_panic("Cannot create alloc ring for port%u\n",
		          vport_info->vportid);
	}

	/* Assign values to in and out port structures */
	port_in_params->ops = &rte_port_ivshm_reader_ops;
	port_in_params->arg_create = port_reader_params;
	port_in_params->f_action = NULL;
	port_in_params->arg_ah = &vport_info->vportid;
	port_in_params->burst_size = PORT_CLIENT_RX_BURST_SIZE;

	for(i = 0; i < RTE_MAX_LCORE; i++) {
		port_out_params = &vport_info->port_out_params[i];
		port_out_params->ops = &rte_port_ivshm_writer_ops;
		port_out_params->arg_create = port_writer_params;
		port_out_params->f_action = ovdk_stats_port_out_update;
		port_out_params->f_action_bulk = ovdk_stats_port_out_update_bulk;
		port_out_params->arg_ah = &vport_info->vportid;
	}

	return 0;
}
