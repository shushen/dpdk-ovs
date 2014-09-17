/*
 * Copyright 2012-2014 Intel Corporation All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include <rte_config.h>
#include <rte_ethdev.h>

#include <inttypes.h>

#include "netdev-provider.h"
#include "packets.h"
#include "vlog.h"
#include "netdev-dpdk.h"
#include "dpif-dpdk.h"

#ifdef PG_DEBUG
#define DPDK_DEBUG() printf("NETDEV-DPDK.c %s Line %d\n", __FUNCTION__, __LINE__);
#else
#define DPDK_DEBUG()
#endif

#define MAX_PHYPORTS 16

VLOG_DEFINE_THIS_MODULE(netdev_dpdk);

static int
netdev_dpdk_init(void)
{
    DPDK_DEBUG()

    return 0;
}

static void
netdev_dpdk_run(void)
{
    DPDK_DEBUG()
}

static void
netdev_dpdk_wait(void)
{
    DPDK_DEBUG()
}

static struct netdev *
netdev_dpdk_alloc(void)
{
    return xzalloc(sizeof(struct netdev));
}

static int
netdev_dpdk_construct(struct netdev *netdev OVS_UNUSED)
{
    return 0;

}

static void
netdev_dpdk_destruct(struct netdev *netdev OVS_UNUSED)
{
    ;

}

static void
netdev_dpdk_dealloc(struct netdev *netdev)
{
    free(netdev);
}

static int
netdev_dpdk_set_etheraddr(struct netdev *netdev_ OVS_UNUSED,
                           const uint8_t mac[ETH_ADDR_LEN] OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static int
netdev_dpdk_get_etheraddr(const struct netdev *netdev_ OVS_UNUSED,
                           uint8_t mac[ETH_ADDR_LEN] OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static void
netdev_stats_from_dpdk_vport_stats(struct netdev_stats *dst,
                                   struct ovdk_port_stats *src)
{
    dst->rx_packets = src->rx;
    dst->tx_packets = src->tx;
    dst->rx_bytes = src->rx_bytes;
    dst->tx_bytes = src->tx_bytes;
    dst->rx_errors = src->rx_error;
    dst->tx_errors = src->tx_error;
    dst->rx_dropped = src->rx_drop;
    dst->tx_dropped = src->tx_drop;
    dst->multicast = UINT64_MAX;
    dst->collisions = UINT64_MAX;
    dst->rx_length_errors = UINT64_MAX;
    dst->rx_over_errors = UINT64_MAX;
    dst->rx_crc_errors = UINT64_MAX;
    dst->rx_frame_errors = UINT64_MAX;
    dst->rx_fifo_errors = UINT64_MAX;
    dst->rx_missed_errors = UINT64_MAX;
    dst->tx_aborted_errors = UINT64_MAX;
    dst->tx_carrier_errors = UINT64_MAX;
    dst->tx_fifo_errors = UINT64_MAX;
    dst->tx_heartbeat_errors = UINT64_MAX;
    dst->tx_window_errors = UINT64_MAX;
}

static int
netdev_dpdk_get_stats(const struct netdev *netdev_,
                       struct netdev_stats *stats)
{
    struct ovdk_port_stats reply;
    int error = 0;

    DPDK_DEBUG()

    error = dpif_dpdk_port_get_stats(netdev_get_name(netdev_), &reply);
    if (!error) {
        netdev_stats_from_dpdk_vport_stats(stats, &reply);
    }

    return error;
}

static int
netdev_dpdk_update_flags(struct netdev *netdev OVS_UNUSED, enum netdev_flags off OVS_UNUSED,
                          enum netdev_flags on OVS_UNUSED, enum netdev_flags *old_flagsp)
{
    DPDK_DEBUG()

    *old_flagsp = NETDEV_UP;

    return 0;
}

/* Allocate memory for a netdev_dpdk_phyport */
static struct netdev *
netdev_dpdk_phy_alloc(void)
{
    struct netdev_dpdk_phyport *phyport = xzalloc(sizeof(*phyport));

    return &phyport->phy_dev;
}

/* Initialize the derived state for 'netdev' */
static int
netdev_dpdk_phy_construct(struct netdev *netdev)
{
    struct netdev_dpdk_phyport *port = NETDEV_DPDK_PHYPORT_CAST(netdev);

    port->port_id = 0;
    ovs_mutex_init(&port->mutex);

    return 0;
}

/* Uninitialize the derived state for 'netdev' */
static void
netdev_dpdk_phy_destruct(struct netdev *netdev)
{
    struct netdev_dpdk_phyport *port = NETDEV_DPDK_PHYPORT_CAST(netdev);

    ovs_mutex_destroy(&port->mutex);
}

/* Free memory associated with a netdev_dpdk_phyport */
static void
netdev_dpdk_phy_dealloc(struct netdev *netdev)
{
	if (netdev == NULL) {
		VLOG_ERR("netdev cannot be dealloced as it is NULL ");
		return;
	}

    free(NETDEV_DPDK_PHYPORT_CAST(netdev));
}

/* Set the configuration of 'netdev' to 'args' */
static int
netdev_dpdk_phy_set_config(struct netdev* netdev, const struct smap *args)
{
    struct netdev_dpdk_phyport *port = NETDEV_DPDK_PHYPORT_CAST(netdev);
    const char *name;
    const char *port_id;
    uint32_t port_no;

    name = netdev_get_name(netdev);

    /* Retrieve ID of physical port from command line */
    port_id = smap_get(args, "port");

    if (!port_id) {
        VLOG_ERR("%s: Physical port type requires valid 'portid' argument",
                     name);
        return EINVAL;
    }

    port_no = (uint32_t) atoi(port_id);
    if (port_no >= MAX_PHYPORTS) {
        VLOG_ERR("%s: Invalid Physical port argument", name);
        return EINVAL;
    }

    /* Obtain handle to netdev_dpdk_phyport from netdev, and set its port_id
     * field. This value will later be used to connect the vport to the correct
     * rings associated with physical port 'port_no'  */
    ovs_mutex_lock(&port->mutex);
    port->port_id = port_no;
    ovs_mutex_unlock(&port->mutex);

    return 0;
}

/* Note: each of the following netdevs - with the exception of the 'dpdkphy'
 *  netdev - are exactly the same bar the "type" string. This is necessary in
 *  order to allow differentiation between different port types when
 *  adding/modifying/querying ports in the datapath. The 'dpdkphy' netdev uses
 *  some slightly different functions in order to allow datapath port number
 *  selection via the dpif */

const struct netdev_class netdev_dpdk_client_class =
{
    "dpdkclient",
    netdev_dpdk_init,
    netdev_dpdk_run,
    netdev_dpdk_wait,
    netdev_dpdk_alloc,
    netdev_dpdk_construct,
    netdev_dpdk_destruct,
    netdev_dpdk_dealloc,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_set_etheraddr,
    netdev_dpdk_get_etheraddr,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_get_stats,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_update_flags,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const struct netdev_class netdev_dpdk_kni_class =
{
    "dpdkkni",
    netdev_dpdk_init,
    netdev_dpdk_run,
    netdev_dpdk_wait,
    netdev_dpdk_alloc,
    netdev_dpdk_construct,
    netdev_dpdk_destruct,
    netdev_dpdk_dealloc,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_set_etheraddr,
    netdev_dpdk_get_etheraddr,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_get_stats,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_update_flags,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const struct netdev_class netdev_dpdk_phy_class =
{
    "dpdkphy",
    netdev_dpdk_init,
    netdev_dpdk_run,
    netdev_dpdk_wait,
    netdev_dpdk_phy_alloc,
    netdev_dpdk_phy_construct,
    netdev_dpdk_phy_destruct,
    netdev_dpdk_phy_dealloc,
    NULL,
    netdev_dpdk_phy_set_config,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_set_etheraddr,
    netdev_dpdk_get_etheraddr,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_get_stats,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_update_flags,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const struct netdev_class netdev_dpdk_veth_class =
{
    "dpdkveth",
    netdev_dpdk_init,
    netdev_dpdk_run,
    netdev_dpdk_wait,
    netdev_dpdk_alloc,
    netdev_dpdk_construct,
    netdev_dpdk_destruct,
    netdev_dpdk_dealloc,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_set_etheraddr,
    netdev_dpdk_get_etheraddr,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_get_stats,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_update_flags,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const struct netdev_class netdev_dpdk_vhost_class =
{
    "dpdkvhost",
    netdev_dpdk_init,
    netdev_dpdk_run,
    netdev_dpdk_wait,
    netdev_dpdk_alloc,
    netdev_dpdk_construct,
    netdev_dpdk_destruct,
    netdev_dpdk_dealloc,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_set_etheraddr,
    netdev_dpdk_get_etheraddr,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_get_stats,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_update_flags,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const struct netdev_class netdev_dpdk_internal_class =
{
    "internal",
    netdev_dpdk_init,
    netdev_dpdk_run,
    netdev_dpdk_wait,
    netdev_dpdk_alloc,
    netdev_dpdk_construct,
    netdev_dpdk_destruct,
    netdev_dpdk_dealloc,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_set_etheraddr,
    netdev_dpdk_get_etheraddr,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_get_stats,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_update_flags,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const struct netdev_class netdev_dpdk_memnic_class =
{
    "dpdkmemnic",
    netdev_dpdk_init,
    netdev_dpdk_run,
    netdev_dpdk_wait,
    netdev_dpdk_alloc,
    netdev_dpdk_construct,
    netdev_dpdk_destruct,
    netdev_dpdk_dealloc,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_set_etheraddr,
    netdev_dpdk_get_etheraddr,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_get_stats,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    netdev_dpdk_update_flags,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
