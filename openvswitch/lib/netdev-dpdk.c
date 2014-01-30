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

#include "common.h"
#include "netdev-provider.h"
#include "packets.h"
#include "vlog.h"
#include "dpif-dpdk.h"

#ifdef PG_DEBUG
#define DPDK_DEBUG() printf("NETDEV-DPDK.c %s Line %d\n", __FUNCTION__, __LINE__);
#else
#define DPDK_DEBUG()
#endif

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
netdev_dpdk_get_etheraddr(const struct netdev *netdev_,
                           uint8_t mac[ETH_ADDR_LEN])
{
    const char *name = netdev_get_name(netdev_);
    uint8_t addr[ETH_ADDR_LEN];
    uint8_t port = 0 ;

    DPDK_DEBUG()

    memset(addr, 0, ETH_ADDR_LEN);

    if (!strncmp(name, DPDK_PORT_PREFIX, DPDK_PORT_PREFIX_LEN)) {
        port = (uint8_t)strtoumax(name + DPDK_PORT_PREFIX_LEN,
                                  NULL, BASE10);
    }

    addr[ETH_ADDR_LEN - 1] = port;

    memcpy(mac, addr, ETH_ADDR_LEN);

    return 0;
}

static void
netdev_stats_from_dpdk_vport_stats(struct netdev_stats *dst,
                                   struct dpif_dpdk_vport_stats *src)
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
    struct dpif_dpdk_vport_stats reply;
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

static unsigned int
netdev_dpdk_change_seq(const struct netdev *netdev OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

const struct netdev_class netdev_dpdk_class =
{
    "dpdk",
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
    netdev_dpdk_change_seq,
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
    netdev_dpdk_change_seq,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

