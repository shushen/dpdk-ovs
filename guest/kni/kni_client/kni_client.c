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
#include <getopt.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>

#include <rte_memzone.h>
#include <rte_string_fns.h>
#include <rte_kni.h>
#include <rte_config.h>
#include <exec-env/rte_kni_common.h>

#include <ovs-vport.h>
#include "kni-types.h"

#define RTE_LOGTYPE_APP      RTE_LOGTYPE_USER1
#define QUEUE_NAME_SIZE      32
#define MBUF_OVERHEAD        (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define RX_MBUF_DATA_SIZE    2048
#define MBUF_SIZE            (RX_MBUF_DATA_SIZE + MBUF_OVERHEAD)

/* list of port names used by the client */
static char *port_names[MAX_KNI_PORTS];
/* Number of enabled ports */
static unsigned ports_n = 0;

static struct rte_kni kni_list[MAX_KNI_PORTS];
static int kni_fd = 0;

/* Function Prototypes */
static int
create_kni_device(struct rte_kni *kni_dev, const char *port_name,
		const unsigned kni_group_id);
static int
kni_change_mtu(uint8_t port_id, unsigned new_mtu);
static int
kni_config_network_interface(uint8_t port_id, uint8_t if_up);

static struct rte_kni_ops kni_ops = {
	.change_mtu = kni_change_mtu,
	.config_network_if = kni_config_network_interface,
};


/* Called by the driver when the kernel calls the
 * ndo_set_mtu function for the driver.
 */
int
kni_change_mtu(uint8_t port_id __rte_unused, unsigned new_mtu __rte_unused)
{
	RTE_LOG(INFO, KNI, "Changing MTU is not supported\n");
	return -EINVAL;
}

/* Called by the driver when the kernel calls the
 * ndo_open function. We return success as we have
 * no real net device to configure.
 */
int
kni_config_network_interface(uint8_t port_id __rte_unused,
                             uint8_t if_up __rte_unused)
{
	return 0;
}

/*
 * print a usage message
 */
static void
usage(const char *progname)
{
	printf("\nUsage: %s [EAL args] -- -p <port_name> [-p <port_name>...]\n",
			progname);
}

/*
 * Parse the application arguments to the client app.
 */
static int
parse_app_args(int argc, char *argv[])
{
	int option_index = 0, opt = 0;
	char **argvopt = argv;
	const char *progname = NULL;

	progname = argv[0];

	while ((opt = getopt_long(argc, argvopt, "p:", NULL,
		&option_index)) != EOF){
		switch (opt){
			case 'p':
				if (ports_n == MAX_KNI_PORTS) {
					RTE_LOG(ERR, APP, "Too many KNI ports have been requested\n");
					return -1;
				}

				if (ovs_vport_is_vport_name_valid(optarg) < 0)
					return -1;

				port_names[ports_n++] = optarg;
				break;
			default:
				usage(progname);
				return -1;
		}
	}

	return 0;
}

/* Fill the dev_info struct and call the ioctl so the
 * kni device is created
 */
int
create_kni_device(struct rte_kni *kni_dev, const char *port_name,
		const unsigned kni_group_id)
{
	const struct rte_memzone *mz = NULL;
	struct rte_kni_device_info dev_info;

	if (kni_dev->in_use != 0) {
		RTE_LOG(ERR, KNI, "Port '%s' has been used\n", port_name);
		return -1;
	}

	dev_info.group_id = kni_group_id;

	rte_snprintf(dev_info.name, RTE_KNI_NAMESIZE, "vEth_%s", port_name);

	/* We store the guest virtual address in our kni structure and
	 * write the physical address (I/O mem address not RAM address)
	 * into the dev struct to be sent to the driver
	 */

	/* TX fifo */
	mz = ovs_vport_kni_lookup_tx_fifo(port_name);
	if (mz == NULL)
		return -1;
	kni_dev->tx_q = mz->addr;
	dev_info.tx_phys = mz->ioremap_addr;

	/* RX fifo */
	mz = ovs_vport_kni_lookup_rx_fifo(port_name);
	if (mz == NULL)
		return -1;
	kni_dev->rx_q = mz->addr;
	dev_info.rx_phys = mz->ioremap_addr;

	/* ALLOC fifo */
	mz = ovs_vport_kni_lookup_alloc_fifo(port_name);
	if (mz == NULL)
		return -1;
	kni_dev->alloc_q = mz->addr;
	dev_info.alloc_phys = mz->ioremap_addr;

	/* FREE fifo */
	mz = ovs_vport_kni_lookup_free_fifo(port_name);
	if (mz == NULL)
		return -1;
	kni_dev->free_q = mz->addr;
	dev_info.free_phys = mz->ioremap_addr;

	/* REQUEST fifo */
	mz = ovs_vport_kni_lookup_req_fifo(port_name);
	if (mz == NULL)
		return -1;
	kni_dev->req_q = mz->addr;
	dev_info.req_phys = mz->ioremap_addr;

	/* RESPONSE fifo */
	mz = ovs_vport_kni_lookup_resp_fifo(port_name);
	if (mz == NULL)
		return -1;
	kni_dev->resp_q = mz->addr;
	dev_info.resp_phys = mz->ioremap_addr;

	/* SYNC fifo */
	mz = ovs_vport_kni_lookup_sync_fifo(port_name);
	if (mz == NULL)
		return -1;
	kni_dev->sync_addr = mz->addr;
	dev_info.sync_va = mz->addr;
	dev_info.sync_phys = mz->ioremap_addr;

	mz = ovs_vport_guest_lookup_packet_mempools_memzone();
	if (mz == NULL)
		return -1;

	dev_info.mbuf_va = mz->addr;
	dev_info.mbuf_phys = mz->ioremap_addr;

	kni_dev->mbuf_size = MBUF_SIZE;
	/* Configure the buffer size which will be checked in kernel module */
	dev_info.mbuf_size = kni_dev->mbuf_size;
	dev_info.mempool_size = mz->len;

	memcpy(&kni_dev->ops, &kni_ops, sizeof(struct rte_kni_ops));

	if (ioctl(kni_fd, RTE_KNI_IOCTL_CREATE, &dev_info) < 0) {
		RTE_LOG(ERR, KNI, "ioctl call on /dev/%s failed", KNI_DEVICE);
		return -1;
	}

	kni_dev->in_use = 1;
	return 0;
}


/*
 * Application main function - loops through
 * receiving and processing packets. Never returns
 */
int
main(int argc, char *argv[])
{
	int retval = 0;
	uint8_t port = 0;
	char *port_name;

	if ((retval = rte_eal_init(argc, argv)) < 0) {
		RTE_LOG(INFO, APP, "EAL init failed.\n");
		return -1;
	}

	argc -= retval;
	argv += retval;
	if (parse_app_args(argc, argv) < 0)
		rte_exit(EXIT_FAILURE, "Invalid command-line arguments\n");

	memset(kni_list, 0, sizeof(struct rte_kni) * MAX_KNI_PORTS);

	/* Open KNI or exit */
	kni_fd = open("/dev/" KNI_DEVICE, O_RDWR);
	if (kni_fd < 0) {
		RTE_LOG(ERR, KNI, "Can not open /dev/%s\n", KNI_DEVICE);
		return -1;
	}

	/* Lookup for vports struct */
	if (ovs_vport_lookup_vport_info() == NULL)
		return -1;

	/* Initialise the devices for each port*/
	for (port = 0; port < ports_n; port++) {
		port_name = port_names[port];
		RTE_LOG(INFO, KNI, "Attaching queues for port '%s'\n", port_name);
		if (create_kni_device(&kni_list[port], port_name, port) < 0)
			return -1;
	}

	RTE_LOG(INFO, KNI, "\nKNI client handling packets \n");
	RTE_LOG(INFO, KNI, "[Press Ctrl-C to quit ...]\n");

	for (;;) {
		for (port = 0; port < ports_n; port++) {
			/* Sleep to reduce processor load. As long as we respond
			 * before rtnetlink times out we will still be able to ifup
			 * and change mtu
			 */
			sleep(1);
			rte_kni_handle_request(&kni_list[port]);
		}
	}

	return 0;
}

