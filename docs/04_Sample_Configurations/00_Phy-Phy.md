The simplest configuration for Intel® DPDK is the Phy-Phy configuration - a simple forwarding of traffic from one physical NIC interface to another. This section contains instructions on how to configure both system and vSwitch for this setup.

______

## Overview

This guide covers a Phy->Phy configuration using IVSHM vports:

```
                                                         __
    +--------------------------------------------------+   |
    |                                                  |   |
    |              +-=------------------+              |   |
    |              |                    v              |   |  host
    |   +--------------+            +--------------+   |   |
    |   |   phy port   |  ovs_dpdk  |   phy port   |   |   |
    +---+--------------+------------+--------------+---+ __|
               ^                           :
               |                           |
               :                           v
    +--------------------------------------------------+
    |                                                  |
    |                traffic generator                 |
    |                                                  |
    +--------------------------------------------------+
```

## Initial Switch Setup

On the host, remove any configuration associated with a previous run of the switch:

```bash
pkill -9 ovs
rm -rf /usr/local/var/run/openvswitch/
rm -rf /usr/local/etc/openvswitch/
mkdir -p /usr/local/var/run/openvswitch/
mkdir -p /usr/local/etc/openvswitch/
rm -f /tmp/conf.db
```

Configure some environment variables

```bash
cd openvswitch
export OPENVSWITCH_DIR=$(pwd)
```

Initialise the Open vSwitch database server:

```bash
./ovsdb/ovsdb-tool create /usr/local/etc/openvswitch/conf.db \
  $OPENVSWITCH_DIR/vswitchd/vswitch.ovsschema
./ovsdb/ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock \
  --remote=db:Open_vSwitch,Open_vSwitch,manager_options &
```

Add a bridge to the switch:

```bash
./utilities/ovs-vsctl --no-wait add-br br0 -- set Bridge br0 datapath_type=dpdk
```

Add ports to the bridge:

```bash
./utilities/ovs-vsctl --no-wait add-port br0 port16 -- set Interface port16 \
  type=dpdkphy ofport_request=16 option:port=1
./utilities/ovs-vsctl --no-wait add-port br0 port17 -- set Interface port17 \
  type=dpdkphy ofport_request=17 option:port=2
```

Confirm the ports have been successfully added:

```bash
./utilities/ovs-vsctl show
```

You should see something like this:

```bash
00000000-0000-0000-0000-000000000000
    Bridge "br0"
        Port "br0"
            Interface "br0"
                type: internal
        Port "port16"
            Interface "port16"
                type: dpdkphy
                options: {port="1"}
        Port "port17"
            Interface "port17"
                type: dpdkphy
                options: {port="2"}
```

Start `ovs_dpdk`:

```bash
./datapath/dpdk/build/ovs_dpdk -c 0x0F -n 4 --proc-type primary \
  --base-virtaddr=<virt_addr> -- -p 0x03 --stats=5 --vswitchd=0 \
  --client_switching_core=1 --config="(0,0,2),(1,0,3)"
```

Start the Open vSwitch daemon:

```bash
./vswitchd/ovs-vswitchd -c 0x100 --proc-type=secondary -- \
  --pidfile=/tmp/vswitchd.pid
```

______

## Flow Table Setup

Delete the default flow entries from the bridge:

```bash
./utilities/ovs-ofctl del-flows br0
```

Add flow entries to switch packets from `port16` (Phy 0) to `port17` (Phy 1):

```bash
./utilities/ovs-ofctl add-flow br0 in_port=16,dl_type=0x0800,nw_src=1.1.1.1,\
nw_dst=6.6.6.2,idle_timeout=0,action=output:17
```

______

## Run the Test

Start transmission of packets from the packet generator. If setup correctly, packets can be seen returning to the transmitting port from the DUT.

______
