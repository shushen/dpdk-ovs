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

#ifndef NETDEV_DPDK_H
#define NETDEV_DPDK_H 1

#include "netdev-provider.h"

/* Defines the implementation-agnostic (i.e. struct netdev) and
 * implementation-specific (i.e. port_id) information needed to
 * bind a physical network device port to a specific vport.
 */
struct netdev_dpdk_phyport
{
    struct netdev phy_dev;

    /* Protects the port_id member */
    struct ovs_mutex mutex;

    /* Index of the physical port */
    uint32_t port_id;
};

/* Returns netedv's parent netdev_dpdk_phyport struct */
#define NETDEV_DPDK_PHYPORT_CAST(netdev) \
    (CONTAINER_OF(netdev, struct netdev_dpdk_phyport, phy_dev))

#endif /* netdev-dpdk.h */
