/*
 * Copyright 2012-2013 Intel Corporation All Rights Reserved.
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

#include <rte_config.h>
#include <rte_ethdev.h>

#include <inttypes.h>

#include "common.h"
#include "netdev-provider.h"
#include "packets.h"
#include "vlog.h"

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

static int
netdev_dpdk_create(const struct netdev_class *class, const char *name,
                    struct netdev_dev **netdev_devp)
{
    DPDK_DEBUG()

    *netdev_devp = xzalloc(sizeof(**netdev_devp));
    netdev_dev_init(*netdev_devp, name, class);

    return 0;
}

static void
netdev_dpdk_destroy(struct netdev_dev *netdev_dev_)
{
    DPDK_DEBUG()

    free(netdev_dev_);
}

static int
netdev_dpdk_open(struct netdev_dev *netdev_dev_, struct netdev **netdevp)
{
    DPDK_DEBUG()

    *netdevp = xzalloc(sizeof(**netdevp));
    netdev_init(*netdevp, netdev_dev_);

    return 0;
}

static void
netdev_dpdk_close(struct netdev *netdev_)
{
    const char *name = netdev_get_name(netdev_);
    if (!strncmp(name, DPDK_PORT_PREFIX, DPDK_PORT_PREFIX_LEN)) {
        netdev_assert_class(netdev_, &netdev_dpdk_class);
    }

    DPDK_DEBUG()

    free(netdev_);
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

static int
netdev_dpdk_get_stats(const struct netdev *netdev_ OVS_UNUSED,
                       struct netdev_stats *stats)
{
    DPDK_DEBUG()

    memset(stats, 0, sizeof(*stats));

    return 0;
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
    netdev_dpdk_create,
    netdev_dpdk_destroy,
    NULL,
    NULL,
    netdev_dpdk_open,
    netdev_dpdk_close,
    NULL,
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
    netdev_dpdk_update_flags,
    netdev_dpdk_change_seq
};

const struct netdev_class netdev_internal_class =
{
    "internal",
    netdev_dpdk_init,
    netdev_dpdk_run,
    netdev_dpdk_wait,
    netdev_dpdk_create,
    netdev_dpdk_destroy,
    NULL,
    NULL,
    netdev_dpdk_open,
    netdev_dpdk_close,
    NULL,
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
    netdev_dpdk_update_flags,
    netdev_dpdk_change_seq
};

