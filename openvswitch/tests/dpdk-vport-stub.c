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

#include <rte_string_fns.h>

#include "rte_config.h"
#include "ovdk_vport_types.h"
#include "ovdk_vport_info.h"

static struct rte_ring *
create_ring(const char *name)
{
	struct rte_ring *r;

	r = rte_ring_create(name, 32, SOCKET_ID_ANY, 0);
	if (!r)
		abort();
	return r;
}

void
create_vport_client(struct vport_info *vport, const char *port_name)
{
	struct vport_client *client;
	char ring_name[RTE_RING_NAMESIZE];

	vport->type = OVDK_VPORT_TYPE_CLIENT;
	snprintf(vport->name, sizeof(vport->name), port_name);

	client = &vport->client;

	/* Create rings and store their names */
	snprintf(ring_name, sizeof(ring_name), "%sRX", port_name);
	snprintf(client->port_reader_client_params.rx_ring_name,
	             sizeof(client->port_reader_client_params.rx_ring_name),
	             ring_name);
	create_ring(ring_name);
	snprintf(client->ring_names.rx, sizeof(client->ring_names.rx), ring_name);

	snprintf(ring_name, sizeof(ring_name), "%sTX", port_name);
	snprintf(client->port_writer_client_params.tx_ring_name,
	             sizeof(client->port_writer_client_params.tx_ring_name),
	             ring_name);
	create_ring(ring_name);
	snprintf(client->ring_names.tx, sizeof(client->ring_names.tx), ring_name);

	snprintf(ring_name, sizeof(ring_name), "%sFREE", port_name);
	snprintf(client->port_reader_client_params.free_ring_name,
	             sizeof(client->port_reader_client_params.free_ring_name),
	             ring_name);
	create_ring(ring_name);
	snprintf(client->ring_names.free, sizeof(client->ring_names.free), ring_name);

	snprintf(ring_name, sizeof(ring_name), "%sALLOC", port_name);
	snprintf(client->port_reader_client_params.alloc_ring_name,
	             sizeof(client->port_reader_client_params.alloc_ring_name),
	             ring_name);
	create_ring(ring_name);
	snprintf(client->ring_names.alloc, sizeof(client->ring_names.alloc), ring_name);
}

static const struct rte_memzone *
create_memzone(const char *name)
{
	const struct rte_memzone *mz;

	mz = rte_memzone_reserve(name, 32, SOCKET_ID_ANY, 0);
	if (!mz)
		abort();
	return mz;
}

