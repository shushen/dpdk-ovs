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
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_errno.h>

#include "ovdk_args.h"
#include "ovdk_mempools.h"
#include "ovdk_datapath_messages.h"

#define MAX_PACKET_SIZE         1520
#define MBUF_OVERHEAD           (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define CTRLMBUF_MAX_MBUFS      64000
#define CTRLMBUF_SIZE           1520
#define CTRLMBUF_CACHE_SIZE     128
#define PKTMBUF_MAX_RING_SIZE   2048
/*TODO Max number of mbufs */
#define PKTMBUF_MAX_MBUFS       128000
#define PKTMBUF_CACHE_SIZE      128

#define NO_FLAGS                0
#define SOCKET0                 0

#define RTE_LOGTYPE_APP         RTE_LOGTYPE_USER1

int
ovdk_mempools_init(void)
{
	struct rte_mempool *pktmbuf_pool = NULL;
	struct rte_mempool *ctrlmbuf_pool = NULL;
	uint32_t max_packet_size = OVDK_DEFAULT_MAX_FRAME_SIZE;
	uint32_t pktmbuf_size = 0;
	uint32_t max_frame_size = ovdk_args_get_max_frame_size();

	if (max_packet_size < max_frame_size)
		max_packet_size = max_frame_size;

	pktmbuf_size = max_packet_size + MBUF_OVERHEAD +
	    RTE_MAX(sizeof(struct ovdk_message), sizeof(struct ovdk_upcall));

	RTE_LOG(INFO, APP, "Creating ctrlmbuf pool '%s' [%u mbufs] ...\n",
	        CTRLMBUF_POOL_NAME, CTRLMBUF_MAX_MBUFS);
	ctrlmbuf_pool = rte_mempool_create(CTRLMBUF_POOL_NAME,
	        CTRLMBUF_MAX_MBUFS, CTRLMBUF_SIZE, CTRLMBUF_CACHE_SIZE,
	        0, NULL, NULL, rte_ctrlmbuf_init, NULL, SOCKET0, NO_FLAGS);
	if (ctrlmbuf_pool == NULL)
		rte_panic("Cannot create ctrlmbuf mempool '%s' (%s)\n",
		          CTRLMBUF_POOL_NAME, rte_strerror(rte_errno));

	/* make sure the upcall does not the exceed mbuf headroom */
	if (sizeof(struct ovdk_upcall) >= RTE_PKTMBUF_HEADROOM)
		rte_panic("Upcall exceed mbuf headroom\n");

	RTE_LOG(INFO, APP, "Creating pktmbuf pool '%s' [%u mbufs] [size %u]...\n",
	        PKTMBUF_POOL_NAME, PKTMBUF_MAX_MBUFS, pktmbuf_size);
	pktmbuf_pool = rte_mempool_create(PKTMBUF_POOL_NAME, PKTMBUF_MAX_MBUFS,
	        pktmbuf_size, PKTMBUF_CACHE_SIZE,
	        sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init,
	        NULL, rte_pktmbuf_init, NULL, SOCKET0, NO_FLAGS );
	if (pktmbuf_pool == NULL)
		rte_panic("Cannot create pktmbuf mempool '%s' (%s)\n",
		          PKTMBUF_POOL_NAME, rte_strerror(rte_errno));

	return 0;
}
