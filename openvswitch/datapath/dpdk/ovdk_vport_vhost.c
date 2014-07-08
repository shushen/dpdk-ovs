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
#include <signal.h>

#include <rte_config.h>

#include "ovdk_vport_vhost.h"
#include "ovdk_stats.h"
#include "ovdk_virtio-net.h"
#include "ovdk_vhost-net-cdev.h"
#include "ovdk_vport.h"
#include "ovdk_mempools.h"

#define PORT_VHOST_RX_BURST_SIZE	32
#define PORT_VHOST_TX_BURST_SIZE	32

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

/* Maximum character device basename size. */
#define MAX_BASENAME_SZ 20

/* Character device basename. Can be set by user. */
char dev_basename[MAX_BASENAME_SZ] = "vhost-net";

/* Charater device index. Can be set by user. */
uint32_t dev_index = 0;

uint32_t num_devices = OVDK_MAX_VHOSTS;

/*pthread to run CUSE session*/
static pthread_t tid = 0;

/*
 * This function ensures that a Vhost device can be safely removed.
 * If the core has set the flag below then memory associated
 * with the device can be unmapped.
 */
void
ovdk_vport_vhost_removal_ack(unsigned lcore_id) {
	if (unlikely(pf_dev_removal_flag[lcore_id] == REQUEST_DEV_REMOVAL)) {
		pf_dev_removal_flag[lcore_id] = ACK_DEV_REMOVAL;
	}
}

/* Per-port vhost initialisation */
int
ovdk_vport_vhost_port_init(struct vport_info *vport_info)
{
	unsigned lcore_id = 0;
	struct rte_pipeline_port_in_params *port_in_params = NULL;
	struct rte_pipeline_port_out_params *port_out_params = NULL;
	struct rte_port_vhost_writer_params *port_writer_params = NULL;
	struct rte_port_vhost_reader_params *port_reader_params = NULL;

	port_reader_params = &vport_info->vhost.port_reader_vhost_params;
	port_reader_params->dev = &vport_info->vhost.dev;
	strncpy(port_reader_params->pool_name, PKTMBUF_POOL_NAME, sizeof(port_reader_params->pool_name));

	/* In port */
	port_in_params = &vport_info->port_in_params;
	port_in_params->ops = &rte_port_vhost_reader_ops;
	port_in_params->arg_create = port_reader_params;
	port_in_params->f_action = NULL;
	port_in_params->arg_ah = &vport_info->vportid;
	port_in_params->burst_size = PORT_VHOST_RX_BURST_SIZE;

	/* Out Port on all cores */
	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		port_writer_params = &vport_info->vhost.port_writer_vhost_params[lcore_id];
		port_writer_params->dev = &vport_info->vhost.dev;
		port_writer_params->tx_burst_sz = PORT_VHOST_TX_BURST_SIZE;

		port_out_params = &vport_info->port_out_params[lcore_id];
		port_out_params->ops = &rte_port_vhost_writer_ops;
		port_out_params->arg_create = port_writer_params;
		port_out_params->f_action = ovdk_stats_port_out_update;
		port_out_params->arg_ah = &vport_info->vportid;
	}

	return 0;
}

/*
 * Set virtqueue flags so that we do not receive interrupts.
 */
static void
pf_set_irq_status(struct virtio_net *dev)
{
	dev->virtqueue[VIRTIO_RXQ]->used->flags = VRING_USED_F_NO_NOTIFY;
	dev->virtqueue[VIRTIO_TXQ]->used->flags = VRING_USED_F_NO_NOTIFY;
}

/*
 * Remove a device from ovdk_pf datapath. Synchronization occurs through the
 * use of the lcore dev_removal_flag. Device is made volatile here to avoid
 * re-ordering of dev->remove=1 which can cause an infinite loop in the
 * rte_pause loop.
 */
static void
pf_destroy_device(volatile struct virtio_net *dev)
{
	unsigned lcore = 0;

	/* Remove device from ovdk_pf port */
	if (ovdk_vport_vhost_down((struct virtio_net*) dev)) {
		RTE_LOG(INFO, APP,
			"Vhost device (%s) could not be removed from vport_info array\n",
			dev->port_name);
		dev->flags &= ~VIRTIO_DEV_RUNNING;
		return;
	}

	/* Set the dev_removal_flag on each lcore */
	RTE_LCORE_FOREACH(lcore) {
		pf_dev_removal_flag[lcore] = REQUEST_DEV_REMOVAL;
	}

	/*
	 * Once each core has set the dev_removal_flag to ACK_DEV_REMOVAL we can be
	 * sure that they can no longer access the device removed from the datapath
	 * and that the devices are no longer in use.
	 */

	RTE_LCORE_FOREACH(lcore) {
		while (pf_dev_removal_flag[lcore] != ACK_DEV_REMOVAL) {
			rte_pause();
		}
	}

	dev->flags &= ~VIRTIO_DEV_RUNNING;

	RTE_LOG(INFO, APP, "(%"PRIu64") Device has been removed from ovdk_pf"
			" port %s\n", dev->device_fh, dev->port_name);

}

/*
 * A new device is added to the datapath. The name of the tap device associated
 * with the virtio device is used to match a device with an ovdk_pf port.
 */
static int
pf_new_device(struct virtio_net *dev)
{
	/* Disable notifications. */
	pf_set_irq_status(dev);

	/* Add device to ovdk_pf port. */
	if (ovdk_vport_vhost_up(dev)) {
		RTE_LOG(INFO, APP, "Device (%s) could not be added to vport_info"
				" array\n", dev->port_name);
		return -1;
	}

	dev->flags |= VIRTIO_DEV_RUNNING;

	RTE_LOG(INFO, APP, "(%"PRIu64") Device has been added to"
			" ovdk_pf port %s\n", dev->device_fh, dev->port_name);

	return 0;
}

/* Function pointers to new and destroy callbacks */
static const struct virtio_net_device_ops pf_virtio_net_device_ops =
{
	.new_device =  pf_new_device,
	.destroy_device = pf_destroy_device,
};

void
ovdk_vport_vhost_init(void)
{
	unsigned lcore = 0;
	int ret = 0;
	static pthread_t tid = 0;

	/* Register cuse device with user defined name & callbacks */
	ret = register_cuse_device((char*)&dev_basename, dev_index,
								get_virtio_net_callbacks());

	if (ret != 0)
		rte_exit(EXIT_FAILURE, "CUSE device setup failure.\n");

	/* Set new_device() and destroy_device() callbacks */
	init_virtio_net(&pf_virtio_net_device_ops);

	/* Start CUSE session thread. */
	pthread_create(&tid, NULL, (void*)cuse_session_loop, NULL);

	/* Set dev_removal_flag on each core to zero */
	RTE_LCORE_FOREACH(lcore) {
		pf_dev_removal_flag[lcore] = ACK_DEV_REMOVAL;
	}
}

/*
 * Call to teardown CUSE session.
 */
void
ovdk_vport_vhost_teardown_cuse(void)
{
	call_teardown_cuse();
}

/*
 * Pass an interrupt signal to pthread tid. Used to
 * notify FUSE session event handler running on pthread
 * tid when interrupt has been generated.
 */
void
ovdk_vport_vhost_pthread_kill(void)
{
	pthread_kill(tid,SIGINT);
}
