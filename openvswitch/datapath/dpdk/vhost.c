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


#include <arpa/inet.h>
#include <getopt.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/virtio_net.h>
#include <linux/virtio_ring.h>
#include <signal.h>
#include <stdint.h>
#include <sys/param.h>
#include <unistd.h>

#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <rte_string_fns.h>

#include "vhost.h"
#include "virtio-net.h"
#include "vhost-net-cdev.h"
#include "vport.h"
#include "jobs.h"

/* Macros for printing using RTE_LOG */
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define RTE_LOGTYPE_VHOST_CONFIG RTE_LOGTYPE_USER2

#define ASSERT_IS_MASTER_LCORE(lcore_id) \
	do { \
		if ((lcore_id) != rte_get_master_lcore()) \
			rte_panic("Lcore %u is not master (%s:%u)\n", \
			          (lcore_id), __FUNCTION__, __LINE__); \
	} while(0)

uint32_t num_devices = 0;

/* Character device basename. Can be set by user. */
char dev_basename[MAX_BASENAME_SZ] = "vhost-net";

/* Charater device index. Can be set by user. */
uint32_t dev_index = 0;

/*
 * Set virtqueue flags so that we do not receive interrupts.
 */
static void
set_irq_status (struct virtio_net *dev)
{
	dev->virtqueue[VIRTIO_RXQ]->used->flags = VRING_USED_F_NO_NOTIFY;
	dev->virtqueue[VIRTIO_TXQ]->used->flags = VRING_USED_F_NO_NOTIFY;
}

/*
 * Remove a device from ovs_dpdk data path. Synchonization occurs through the
 * use of the lcore dev_removal_flag. Device is made volatile here to avoid
 * re-ordering of dev->remove=1 which can cause an infinite loop in the
 * rte_pause loop.
 */
static void
destroy_device (volatile struct virtio_net *dev)
{
	unsigned lcore;
	/*
	 * Because we need to wait for other online lcores changing their dev
	 * removal flag, we need to ensure that those lcore are not stopped
	 * while we are waiting.
	 * However since lcores can only be started and stopped by the master
	 * lcore, we just need to ensure that we run on master lcore here.
	 */
	ASSERT_IS_MASTER_LCORE(rte_lcore_id());

	/* Remove device from ovs_dpdk port. */
	if (vport_vhost_down((struct virtio_net*) dev) < 0) {
		RTE_LOG(INFO, APP,
			"Device could not be removed from ovs_dpdk port %s\n",
			dev->port_name);
		dev->flags &= ~VIRTIO_DEV_RUNNING;
		return;
	}
	
	/* Set the dev_removal_flag on each lcore. */
	RTE_LCORE_FOREACH(lcore) {
		dev_removal_flag[lcore] = REQUEST_DEV_REMOVAL;
	}

	/*
	 * Once each core has set the dev_removal_flag to ACK_DEV_REMOVAL we can be sure that
	 * they can no longer access the device removed from the data path and that the devices
	 * are no longer in use.
	 */

	RTE_LCORE_FOREACH(lcore) {
		if (JOBS_LCORE_IS_ONLINE(lcore)) {
			while (dev_removal_flag[lcore] != ACK_DEV_REMOVAL) {
				rte_pause();
			}
		}
	}
	
	dev->flags &= ~VIRTIO_DEV_RUNNING;
	
	RTE_LOG(INFO, APP, "(%"PRIu64") Device has been removed from ovs_dpdk \
		            port %s\n", dev->device_fh, dev->port_name);
}

/*
 * A new device is added to the data path. The name of the tap device associated
 * with the virtio device is used to match a device with an ovs_dpdk port.
 */
static int
new_device (struct virtio_net *dev)
{
	/* Disable notifications. */
	set_irq_status(dev);

	/* Add device to ovs_dpdk port. */
	if (vport_vhost_up(dev) < 0) {
		RTE_LOG(INFO, APP,
			"Device could not be added to ovs_dpdk port %s\n",
			dev->port_name);
		return -1;
	}

	dev->flags |= VIRTIO_DEV_RUNNING;

	RTE_LOG(INFO, APP, "(%"PRIu64") Vhost device has been added to \
		ovs_dpdk port %s\n", dev->device_fh, dev->port_name);

	return 0;
}

/*
 * These callbacks allow devices to be added to the data core when configuration
 * has been fully complete.
 */
static const struct virtio_net_device_ops virtio_net_device_ops =
{
	.new_device =  new_device,
	.destroy_device = destroy_device,
};

/*
 * The initialisation of userspace vhost mainly consists of configuring and
 * initialising the CUSE character device. This will run as a separate thread as
 * it is a blocking function.
 */

int
vhost_init(void)
{
	int ret;
	static pthread_t tid;

/* The log level is only set if DEBUG is set in vhost.h */
#ifdef LOG_LEVEL
	/* Set log level. */
	rte_set_log_level(LOG_LEVEL); 
#endif

	/* one vHost device per OVS virtual port */
	num_devices = MAX_VHOST_PORTS;

	/* Register CUSE device to handle IOCTLs. */
	ret = register_cuse_device((char*)&dev_basename, dev_index,
			           get_virtio_net_callbacks());
	if (ret != 0)
		rte_exit(EXIT_FAILURE, "CUSE device setup failure.\n");

	init_virtio_net(&virtio_net_device_ops);

	/* Start CUSE session thread. */
	pthread_create(&tid, NULL, (void*)cuse_session_loop, NULL);
	
	RTE_LOG(INFO, APP, "Initialising Vhost\n");
	return 0;
}
