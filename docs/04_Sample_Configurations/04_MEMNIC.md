Intel® DPDK vSwitch supports MEMNIC interface. MEMNIC is memory copy-based virtual network interface which can be used for network communication between host and guest. MEMNIC uses the standard unmodified IVSHMEM (Inter VM Shared Memory in QEMU) mechanism to share packets between host and guests applications.

This section contains instructions on how to compile and run a sample application that demonstrates performance of Intel® DPDK vSwitch with MEMNIC integration. It also describes the additional configuration required for both host and client systems to use MEMNIC.

______

## Overview

This guide covers a Phy->VM->Phy configuration using MEMNIC vports:

```
                                                         __
    +--------------------------------------------------+   |
    | guest                                            |   |
    |                   IP forwarding                  |   |
    |              +-=------------------+              |   |
    |              |                    v              |   |
    |   +--------------+            +--------------+   |   |  guest
    |   |    vEth0     |            |    vEth1     |   |   |
    +---+--------------+------------+--------------+---+ __|
               ^                           :
               |                           |
               :                           v             __
    +---+--------------+------------+--------------+---+   |
    |   | memnic vport1|            | memnic vport1|   |   |
    |   +--------------+            +--------------+   |   |
    |          ^                           :           |   |
    |          |                           |           |   |  host
    |          :                           v           |   |
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

______

## Inital Switch Setup

On the host, remove any configuration associated with a previous run of the switch:

```bash
pkill -9 ovs
rm -rf /usr/local/var/run/openvswitch/
rm -rf /usr/local/etc/openvswitch/
mkdir -p /usr/local/var/run/openvswitch/
mkdir -p /usr/local/etc/openvswitch/
rm -f /tmp/conf.db
```

Initialise the Open vSwitch database server:

```bash
./ovsdb-tool create /usr/local/etc/openvswitch/conf.db $OPENVSWITCH_DIR/vswitchd/vswitch.ovsschema
./ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock --remote=db:Open_vSwitch,manager_options &
```

Add a bridge to the switch:

```bash
./ovs-vsctl --no-wait add-br br0 -- set Bridge br0
  datapath_type=dpdk
```

Add ports to the bridge:

```bash
./ovs-vsctl --no-wait add-port br0 ovs_dpdk_16 -- set Interface ovs_dpdk_16
  type=dpdk ofport_request=16
./ovs-vsctl --no-wait add-port br0 ovs_dpdk_17 -- set Interface ovs_dpdk_17
  type=dpdk ofport_request=17
./ovs-vsctl --no-wait add-port br0 ovs_dpdk_48 -- set Interface ovs_dpdk_48
  type=dpdkmemnic ofport_request=48
./ovs-vsctl --no-wait add-port br0 ovs_dpdk_49 -- set Interface ovs_dpdk_49
  type=dpdkmemnic ofport_request=49
```

Confirm the ports have been successfully added:

```bash
./ovs-vsctl show
```

Start `ovs_dpdk`:

```bash
./ovs_dpdk -c 0x0F -n 4 --proc-type=primary --base-virtaddr=<virt_addr>
  -- -p 0x03 -n 2 -m 2 --stats=5 --vswitchd=0 --client_switching_core=1
  --config="(0,0,2),(1,0,3)"
```

ovs_dpdk will create shared memory in `/dev/shm`. The above example, `ovs_dpdk_48` and `ovs_dpdk_49` are created.

```bash
$ ls /dev/shm
ovs_dpdk_48 ovs_dpdk_49
```

Start the Open vSwitch daemon:

```bash
./ovs-vswitchd -c 0x100 --proc-type=secondary -- --pidfile=/tmp/vswitchd.pid
```

______

## Flow Table Setup

Delete the default flow entries from the bridge:

```bash
./ovs-ofctl del-flows br0
```

Add flow entries to switch packets from `Port0` (16) to `MEMNIC0` (48) on the ingress path, and from `MEMNIC1` (49) to `Port1` (17) on the egress path:

```bash
./ovs-ofctl add-flow br0 in_port=16,dl_type=0x0800,nw_src=1.1.1.1,
  nw_dst=6.6.6.2,idle_timeout=0,action=output:48
./ovs-ofctl add-flow br0 in_port=49,dl_type=0x0800,nw_src=1.1.1.1,
  nw_dst=6.6.6.2,idle_timeout=0,action=output:17
```

______

## Guest Setup

### Copy Files

Copy files to the VM making sure the `-L` flag is used within `cp` to follow the symlinks.

```bash
rm -rf /tmp/qemu_share
mkdir -p /tmp/qemu_share
chmod 777 /tmp/qemu_share
mkdir -p /tmp/qemu_share/memnic
cp -a <MEMNIC_DIR>/* /tmp/qemu_share/DPDK
```

### Create VM

Start QEMU, for example:

```bash
./qemu/x86_64-softmmu/qemu-system-x86_64 -c 0x30 --proc-type secondary -n 4
  -- -cpu host -boot c -hda <qemu_imagename.qcow2> -snapshot -m 8192M -smp 2
  --enable-kvm -name 'client 1' -nographic -vnc :1 -pidfile /tmp/vm1.pid
  -drive file=fat:rw:/tmp/qemu_share ,snapshot=off
  -monitor unix:/tmp/vm1monitor,server,nowait \
  -device ivshmem,size=16,shm=/ovs_dpdk_48
  -device ivshmem,size=16,shm=/ovs_dpdk_49
```

______

## Guest Configuration

There are three methods of using MEMNIC within the guest: as an Intel® DPDK application, as a Linux Socket application, and as a combined Intel® DPDK and Linux Socket application. This guide covers the Linux Socket application only.

### Copy Files

Login to the VM and copy across the files

```bash
mkdir -p /mnt/memnic
mount -o iocharset=utf8 /dev/sdb1 /mnt/memnic
mkdir -p /root/memnix
cp -a /mnt/memnic/* /root/memnic
```

### Build `memnic` Module

```bash
cd /root/memnic/linux
make -C /lib/modules/$(uname -r)/build M=$(pwd) clean
make -C /lib/modules/$(uname -r)/build M=$(pwd)
```

### Insert `memnic` Module

Once everything has been built, insert the `memnic` module:

```bash
insmod ./memnic.ko
```

You can ensure that this has completed sucessfully by checking the output of `dmesg`, you should see something like the following:

```bash
dmesg
```

```bash
[ 128.6358613] memnic: module license unspecified taints kernel.
[ 128.635866] Disabling lock debugging due to kernel taint
[ 128.648266] MEMNIC: Probing
[ 128.653518] ACPI: PCI tnterrupt Link [LNKD] enabled at IRQ 16
[ 128.655464] MEMNIC: iomap addr=ffffc90600880000
[ 128.655477] MEMNIC: new vNIC device found
[ 128.679388] MEPINIC DRIVER: Loaded
```

### Configure Network Interfaces

Bring up virtual interfaces:

```bash
ifconfig eth1 hw ether 00:4B:4E:49:30:00
ifconfig eth2 hw ether 00:4B:4E:49:30:01
ifconfig eth1 2.2.2.1/24 up
ifconfig eth2 3.3.3.1/24 up
```

Note that the MEMNIC kernel module does not support setting arbitrary MAC addresses through `ifconfig`, as shown above. Standard MEMNIC implementation will generate a random MAC when `ovs_dpdk` application starts. In order to be able to set an arbitrary MAC address to the virtual interfaces please apply the supplied patch before building the module.

Turn on IP forwarding:

```bash
sysctl -w net.ipv4.ip_forward=1
```

Turn off reverse path filtering:

```bash
sysctl -w net.ipv4.conf.all.rp_filter=0
sysctl -w net.ipv4.conf.eth1.rp_filter=0
```

Add a default gateway:

```bash
route add default gw 6.6.6.2 eth2
```

Add an ARP entry to the default gateway:

```bash
arp -s 6.6.6.2 DE:AD:BE:EF:CA:FE
```

Check both virtual interfaces are up and running

```bash
ifconfig -a
```

______
