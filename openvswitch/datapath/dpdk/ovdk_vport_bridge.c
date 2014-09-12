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
#include <rte_errno.h>

#include "ovdk_vport_bridge.h"
#include "ovdk_mempools.h"
#include "ovdk_stats.h"

int
ovdk_vport_bridge_port_init(struct vport_info *vport_info)
{
	struct rte_pipeline_port_in_params *port_in_params = NULL;
	struct rte_pipeline_port_out_params *port_out_params = NULL;
	struct rte_port_source_params *port_reader_params = NULL;
	struct rte_mempool *mp = NULL;
	unsigned lcore_id = 0;

	mp = rte_mempool_lookup(PKTMBUF_POOL_NAME);
	if (mp == NULL)
		rte_panic("Cannot find mempool '%s' (%s)\n", PKTMBUF_POOL_NAME,
		          rte_strerror(rte_errno));

	/* Check for null type before use*/
	if(vport_info == NULL)
		rte_panic("Cannot init bridge port, invalid vport info\n");

	vport_info->type = OVDK_VPORT_TYPE_BRIDGE;

	port_reader_params = &vport_info->bridge.port_reader_source_params;
	port_reader_params->mempool = mp;

	port_in_params = &vport_info->port_in_params;
	port_in_params->ops = &rte_port_source_ops;
	port_in_params->arg_create = port_reader_params;
	port_in_params->f_action = NULL;
	port_in_params->arg_ah = &vport_info->vportid;
	port_in_params->burst_size = 32;

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		port_out_params = &vport_info->port_out_params[lcore_id];
		port_out_params->ops = &rte_port_sink_ops;
		port_out_params->arg_create = NULL;
		port_out_params->f_action = ovdk_stats_port_out_update;
		port_out_params->f_action_bulk = ovdk_stats_port_out_update_bulk;
		port_out_params->arg_ah = &vport_info->vportid;
	}

	return 0;
}
