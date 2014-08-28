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

#ifndef __OVDK_VPORT_H_
#define __OVDK_VPORT_H_

#include <stdint.h>

#include <rte_config.h>
#include <rte_pipeline.h>

#include "ovdk_virtio-net.h"

void ovdk_vport_init(void);
void ovdk_vport_shutdown(void);
int ovdk_vport_get_in_portid(uint32_t vportid, uint32_t *portid);
int ovdk_vport_set_in_portid(uint32_t vportid, uint32_t portid);
int ovdk_vport_get_out_portid(uint32_t vportid, uint32_t *portid);
int ovdk_vport_set_out_portid(uint32_t vportid, uint32_t portid);
int ovdk_vport_get_vportid(uint32_t port_in_id, uint32_t *vportid);
int ovdk_vport_get_out_params(uint32_t vportid,
                              struct rte_pipeline_port_out_params **params);
int ovdk_vport_get_in_params(uint32_t vportid,
                             struct rte_pipeline_port_in_params **params);
int ovdk_vport_set_port_name(uint32_t vportid, char *port_name);
int ovdk_vport_get_port_name(uint32_t vportid, char *port_name);

int ovdk_vport_vhost_up(struct virtio_net *dev);
int ovdk_vport_vhost_down(struct virtio_net *dev);
int ovdk_vport_port_verify(uint32_t vportid);

#endif /* __OVDK_VPORT_H_ */
