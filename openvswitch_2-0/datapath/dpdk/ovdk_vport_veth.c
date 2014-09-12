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

#include <stdio.h>
#include <pthread.h>

#include <rte_config.h>
#include <rte_string_fns.h>
#include <rte_kni.h>
#include <rte_errno.h>

#include "rte_port_veth.h"
#include "ovdk_vport_veth.h"
#include "ovdk_stats.h"
#include "ovdk_mempools.h"
#include "ovdk_vport_types.h"

#define PORT_VETH_RX_BURST_SIZE     32
#define PORT_VETH_TX_BURST_SIZE     32

#define RTE_LOGTYPE_APP             RTE_LOGTYPE_USER1

#define MAX_PACKET_SZ               2048

static int ovdk_vport_veth_if_init(struct veth_dev **dev, uint8_t port_id);
static int ovdk_vport_veth_if_shutdown(struct veth_dev **dev);

/*
 * Per-port vEth initialisation.
 *
 * Initialises an instance of a vEth port. This involves two steps, (a)
 * configuring the underlying interface and (b) adding ports bound to this
 * interface to the pipelines as in/out ports. The result is stored in the
 * out parameter 'vport_info'.
 */
int
ovdk_vport_veth_port_init(struct vport_info *vport_info)
{
	unsigned lcore_id = 0;
	struct rte_pipeline_port_in_params *port_in_params = NULL;
	struct rte_pipeline_port_out_params *port_out_params = NULL;
	struct rte_port_veth_writer_params *port_writer_params = NULL;
	struct rte_port_veth_reader_params *port_reader_params = NULL;
	struct veth_dev *dev = NULL;

	if(vport_info == NULL)
		rte_panic("Cannot init vEth port - invalid vport info\n");

	RTE_LOG(INFO,
	        USER1,
	        "Initializing vEth port '%u' ...\n",
	        vport_info->vportid);

	/* Initialise the underlying interface */

	dev = malloc(sizeof(struct veth_dev));
	ovdk_vport_veth_if_init(&dev, vport_info->vportid);
	vport_info->veth.dev = dev;

	/* In/reader port config (single core) */

	port_in_params = &vport_info->port_in_params;
	port_reader_params = &vport_info->veth.port_reader_veth_params;

	port_reader_params->dev = &vport_info->veth.dev;

	port_in_params->ops = &rte_port_veth_reader_ops;
	port_in_params->arg_create = port_reader_params;
	port_in_params->f_action = NULL;
	port_in_params->arg_ah = &vport_info->vportid;
	port_in_params->burst_size = PORT_VETH_RX_BURST_SIZE;

	/* Out/writer port config (all cores) */

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		port_writer_params = &vport_info->veth.port_writer_veth_params[lcore_id];
		port_out_params = &vport_info->port_out_params[lcore_id];

		port_writer_params->dev = &vport_info->veth.dev;
		port_writer_params->tx_burst_sz = PORT_VETH_TX_BURST_SIZE;

		port_out_params->ops = &rte_port_veth_writer_ops;
		port_out_params->arg_create = port_writer_params;
		port_out_params->f_action = ovdk_stats_port_out_update;
		port_out_params->f_action_bulk = ovdk_stats_port_out_update_bulk;
		port_out_params->arg_ah = &vport_info->vportid;
	}

	return 0;
}

/*
 * Initialise the underlying KNI interface and mutex for vEth.
 */
static int
ovdk_vport_veth_if_init(struct veth_dev **dev, uint8_t port_id)
{
	struct rte_mempool *mp = NULL;
	struct rte_kni *veth = NULL;
	struct rte_kni_conf conf;

	if (dev == NULL || *dev == NULL)
		rte_panic("Cannot create vEth interface - invalid device\n");

	mp = rte_mempool_lookup(PKTMBUF_POOL_NAME);
	if (mp == NULL)
		rte_panic("Unable to lookup pktmbuf pool '%s' (%s)\n",
		          PKTMBUF_POOL_NAME, rte_strerror(rte_errno));

	/* Clear conf at first */
	memset(&conf, 0, sizeof(conf));
	snprintf(conf.name, RTE_KNI_NAMESIZE, "vEth%u",
	         port_id - OVDK_VPORT_TYPE_VETH);
	conf.group_id = (uint16_t)port_id;
	conf.mbuf_size = MAX_PACKET_SZ;

	veth = rte_kni_alloc(mp, &conf, NULL);
	if (veth == NULL)
		rte_exit(EXIT_FAILURE, "Failed to create kni for port: '%d'\n",
		         port_id);

	if (rte_kni_get(conf.name) != veth)
		rte_exit(EXIT_FAILURE, "Failed to get kni dev for port: '%d'\n",
		         port_id);

	RTE_LOG(INFO, APP, "vEth device '%s' has been added to"
	        " ovdk_pf port '%d'\n", conf.name, port_id);

	(**dev).kni = veth;
	pthread_mutex_init(&(*dev)->mutex, NULL);

	return 0;
}

/*
 * Per-port vEth shutdown.
 *
 * Deallocate an instance of a vEth port. The resulting NULL kni device is
 * stored in the out parameter 'vport_info'.
 *
 * On success, returns 0. On failure, a negative value is returned.
 */
int
ovdk_vport_veth_port_shutdown(struct vport_info *vport_info)
{
	int ret = 0;

	if(vport_info == NULL)
		rte_panic("Cannot shutdown vEth port - invalid vport info\n");

	RTE_LOG(INFO,
	        USER1,
	        "Shutting down vEth port '%u' ...\n",
	        vport_info->vportid);

	ret = ovdk_vport_veth_if_shutdown(&vport_info->veth.dev);
	if (ret)
		RTE_LOG(ERR,
		        USER1,
		        "Failed to release KNI device for vEth port '%u' ...\n",
		        vport_info->vportid);

	return ret;
}

/*
 * shutdown the underlying KNI interface for vEth.
 */
static int
ovdk_vport_veth_if_shutdown(struct veth_dev **dev)
{
	if (dev == NULL || *dev == NULL)
		return -1;

	if (rte_kni_release((*dev)->kni))
		return -1;

	(*dev)->kni = NULL;
	pthread_mutex_destroy(&(*dev)->mutex);

	return 0;
}
