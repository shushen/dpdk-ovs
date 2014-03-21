This section contains instructions on how to compile and run a sample application that demonstrates performance of an Intel® DPDK-accelerated version of Userspace Vhost for IO virtualization. For more information on the Userspace Vhost implementation refer to the [*Intel® Data Plane Development Kit (Intel DPDK) - Sample Applications User Guide*][intel-dpdksample].

```
                                                         __
    +---------------------------------------------------+   |
    |   guest                                           |   |
    |   +-------------------------------------------+   |   |
    |   |                  TESTPMD                  |   |   |
    |   +-------------------------------------------+   |   |
    |       ^                                  :        |   |
    |       |                                  |        |   |  guest
    |       :                                  v        |   |
    |   +-------------------------------------------+   |   |
    |   |                   DPDK                    |   |   |
    +---+---------------+-----------+---------------+---+ __|
            ^                                  :
            |                                  |
            :                                  v         __
    +---+---------------+----------+--------------=+---+   |
    |   | vhost vport 0 |          | vhost vport 1 |   |   |
    |   +---------------+          +---------------+   |   |
    |       ^      |                    ^      :       |   |
    |       |      +-=------------------+      |       |   |  host
    |       :                                  v       |   |
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


**Note:** Hugepages are required for both the guest and the vswitch application. Ideally there should be enough hugepages on a single socket to maximise performance. For example, to run a guest with 2GB of memory and running the vswitch with 2GB of memory there should be 8x1GB hugepages allocated (4 on each socket).

## Prerequisites

There are 4 extra requirements in order to use Userspace Vhost devices in Intel® DPDK
vSwitch.

### Insert the CUSE kernel module

```bash
modprobe cuse
```

**Note:** If this fails, you likely don't have `kernel-modules-extra` installed. Please review the [Installation Guide][doc-installation] for more information on required packages.

### Insert the fd_link module

```bash
insmod ./openvswitch/datapath/dpdk/fd_link/fd_link.ko
```

### Remove vhost-net module

The default configuration will use the /dev/vhost-net character device. This device is also used for legacy Vhost and so it is important that this module is removed if it is being used.

```bash
rmmod vhost-net
```

If the module has been removed but the device /dev/vhost-net still exists then it should be removed manually.

```bash
rm /dev/vhost-net
```

**Note:** It is a known issue that on start-up this device can exist without the legacy Vhost module being inserted.

### Install fuse library

Intel® DPDK Open vSwitch now includes the fuse library at compile time. This library must exist on the system or the application will fail to compile. This can be installed through the distributions repository or it can be [built from source][fuse-source]

______

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
./utilities/ovs-vsctl --no-wait add-port br0 port80 -- set Interface port80 \
  type=dpdkvhost ofport_request=80
./utilities/ovs-vsctl --no-wait add-port br0 port81 -- set Interface port81 \
  type=dpdkvhost ofport_request=81
```

Confirm the ports have been successfully added:

```bash
./ovs-vsctl show
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
        Port "port80"
            Interface "port80"
                type: dpdkvhost
        Port "port81"
            Interface "port81"
                type: dpdkvhost
```

Start `ovs_dpdk`:

Start `ovs_dpdk`:

```bash
./datapath/dpdk/build/ovs_dpdk -c 0x0F -n 4 --proc-type primary \
  --base-virtaddr=<virt_addr> --socket-mem 2048,2048 -- -p 0x03 -n 2 -h 2 \
  --stats=5 --vswitchd=0 --client_switching_core=1 --config="(0,0,2),(1,0,3)"
```

Note the additional `--socket-mem` option. This ensures the DPDK app does not use all available hugepage memory on the system.

Start the Open vSwitch daemon:

```bash
./vswitchd/ovs-vswitchd -c 0x100 --proc-type=secondary -- \
  --pidfile=/tmp/vswitchd.pid
```
______

## Flow Table Setup

Delete the default flow entries from the bridge:

```bash
./ovs-ofctl del-flows br0
```

Add flow entries to switch packets from `Port0` (16) to `vhost0` (80) on the ingress path, and from `vhost1` (81) to `Port1` (17) on the egress path:

```bash
./ovs-ofctl add-flow br0 in_port=16,dl_type=0x0800,nw_src=1.1.1.1,
  nw_dst=6.6.6.2,idle_timeout=0,action=output:80
./ovs-ofctl add-flow br0 in_port=81,dl_type=0x0800,nw_src=1.1.1.1,
  nw_dst=6.6.6.2,idle_timeout=0,action=output:17
```

______

## Set up the Guest

### Copy Files

Intel® DPDK source code must be copied to each guest required. The simplest way to do this step is by copying the required files to a directory on the host and mounting this directory as a drive on the guest. Once the guest is started, the files can be copied from the mounted drive to a local directory. This method has been validated using qcow2 images.

```bash
rm -rf /tmp/qemu_share
mkdir -p /tmp/qemu_share
mkdir -p /tmp/qemu_share/DPDK
mkdir -p /tmp/qemu_share/kni_client
chmod 777 /tmp/qemu_share
cp -aL <DPDK_DIR>/* /tmp/qemu_share/DPDK
```

## Create VM

**Note:** QEMU will fail if `ovs_dpdk` is not already running. The following command line will launch QEMU with two vhost enabled virtio devices. It is important to note that the tap device names should be **identical** to the ovs_dpdk port names:

```bash
sudo ./qemu/x86_64-softmmu/qemu-system-x86_64 -c 0x30 --proc-type secondary -n 4 -- -cpu host -boot c -hda <PATH_TO_IMAGE> -snapshot -m 512 -smp 2 --enable-kvm -name "client 1" -nographic -vnc :1 -net none -no-reboot -mem-path /dev/hugepages -mem-prealloc -netdev type=tap,id=net1,script=no,downscript=no,ifname=ovs_dpdk_80,vhost=on -device virtio-net-pci,netdev=net1,mac=00:00:00:00:00:01,csum=off,gso=off,guest_tso4=off,guest_tso6=off,guest_ecn=off -netdev type=tap,id=net2,script=no,downscript=no,ifname=ovs_dpdk_81,vhost=on -device virtio-net-pci,netdev=net2,mac=00:00:00:00:00:02,csum=off,gso=off,guest_tso4=off,guest_tso6=off,guest_ecn=off -drive file=fat:rw:/tmp/qemu_share,snapshot=off
```

**Note:** Userspace Vhost devices can also be launched with an unmodified QEMU version by excluding the Intel® DPDK related options before the "--".

______

## Guest Configuration

The following configuration must be performed on each Vhost client.

### Copy Files

Login to the VM and copy across the files:

```bash
mkdir –p /mnt/vhost_client
mkdir –p /root/vhost_client
mount –o iocharset=utf8 /dev/sdb1 /mnt/vhost_client
cp –a /mnt/vhost_client/* /root/vhost_client
```

### Build DPDK on the Guest

```bash
cd /root/vhost_client/DPDK
export RTE_SDK=/root/vhost_client/DPDK
export RTE_TARGET=x86_64-ivshmem-linuxapp-gcc
make install T=x86_64-ivshmem-linuxapp-gcc
```

### Build `test-pmd` App

```bash
cd /root/vhost_client/DPDK/app/test-pmd
make clean
make
```

### Execute `test-pmd` App

```bash
modprobe uio
insmod x86_64-ivshmem-linuxapp-gcc/kmod/igb_uio.ko
./tools/pci_unbind.py -b igb_uio 0000:00:03.0 0000:00:04.0
./testpmd -c 0x3 -n 4 --socket-mem 128 -- --burst=64 -i
```

At the `testpmd` prompt, issue the following commands:

```bash
set fwd mac_retry
start
```

______

## Run the Test

Start transmission of packets from the packet generator. If setup correctly, packets can be seen returning to the transmitting port from the DUT.

______

[doc-installation]: 01_Installation.md
[intel-dpdksample]: http://www.intel.com/content/www/us/en/intelligent-systems/intel-technology/intel-dpdk-sample-applications-user-guide.html
[fuse-source]: http://sourceforge.net/projects/fuse/files/fuse-2.X/
