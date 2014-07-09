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

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_string_fns.h>

#include "ovs-vport.h"
#include "ovdk_vport_info.h"
#include "ovdk_mempools.h"

static struct vport_info *stub_vports = NULL;
static struct rte_mempool *pktmbuf_pool;

int
main(int argc, char *argv[])
{
	if (rte_eal_init(argc, argv) < 0)
		return -1;

	static const struct rte_memzone *stub_vports_mz = NULL;

	stub_vports_mz = rte_memzone_reserve(OVDK_MZ_VPORT_INFO,
			sizeof(struct vport_info) * OVDK_MAX_VPORTS, rte_socket_id(), 0);
	assert(stub_vports_mz != NULL);

	stub_vports = stub_vports_mz->addr;
	create_vport_client(&stub_vports[0], "Client1");
	create_vport_client(&stub_vports[1], "Client2");

	assert(stub_vports_mz == ovs_vport_lookup_vport_info());

	pktmbuf_pool = rte_mempool_create(PKTMBUF_POOL_NAME, 32, 16, 32, 32, NULL,
			NULL, NULL, NULL, rte_socket_id(), 0);

	assert(pktmbuf_pool != NULL);

	for (;;)
		sleep(1);

	return 0;
}






