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

#ifndef __OVDK_PIPELINE_H_
#define __OVDK_PIPELINE_H_

#include <rte_config.h>
#include <rte_ring.h>

#include "ovdk_flow.h"
#include "ovdk_action.h"
#include "datapath/dpdk/ovdk_stats_types.h"

void ovdk_pipeline_init(void);

int ovdk_pipeline_flow_add(struct ovdk_flow_key *key, struct ovdk_action *actions,
                           uint8_t num_actions, uint64_t *flow_handle);
int ovdk_pipeline_flow_del(struct ovdk_flow_key *key,
                           int *key_found,
                           struct ovdk_flow_stats *stats);
int ovdk_pipeline_flow_get_actions(struct ovdk_action *actions,
                                uint8_t *num_actions,
                                uint64_t flow_handle);
int ovdk_pipeline_flow_get_stats(struct ovdk_flow_stats *stats,
                                 uint64_t flow_handle);
int ovdk_pipeline_flow_set_stats(struct ovdk_flow_stats *stats,
                                 uint64_t flow_handle);
int ovdk_pipeline_port_in_add(uint32_t vportid, char *vport_name);
int ovdk_pipeline_port_in_del(uint32_t vportid);
int ovdk_pipeline_port_out_add(uint32_t vportid);
int ovdk_pipeline_port_out_del(uint32_t vportid);

int ovdk_pipeline_run(void);

#endif /* __OVDK_PIPELINE_H_ */
