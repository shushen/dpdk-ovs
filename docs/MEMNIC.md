# MEMNIC Getting Started Guide

## MEMNIC Setup

Intel(R) DPDK vSwitch supports MEMNIC interface

### Host Setup

#### Configure Kernel Boot Parameters

Enable hugepage usage within kernel boot options.
Standard Intel(R) DPDK vSwitch configuration requires a single
1 GB hugepage per instance of the vSwitch:

    hugepagesz=1G hugepages=1


#### Build Source Code

Compile Intel(R) DPDK, Intel(R) DPDK vSwitch as described in
the GSG Section 4.0.

#### Set Up the Intel(R) DPDK

Once compilation of the above packages is complete, insert the
Intel(R) DPDK kernel module and mount the hugepage(s).

    # modprobe uio
    # insmod $RTE_SDK/$RTE_TARGET/kmod/igb_uio
    # mount -t hugetlbfs nodev /dev/hugepages

Ensure the hugetlbfs mount point is mounted correctly by running the
`mount` command.

    # mount | grep hugepages

The output of this command should be:

    nodev on /dev/hugepages type hugetlbfs (rw,realtime)

#### Add Ports to the vSwitch

Add flow ports to the switch for the MEMNIC devices (and other devices)
you wish to use, useing `ovs-vsctl`. See GSG Section 8.0 for details on
how to add ports to the switch.

Valid port values for MEMNIC is 48-63.

#### Start Intel(R) DPDK vSwitch (ovs_dpdk)

Start the `ovs_dpdk` application.

    # ./openvswitch/datapath/dpdk/build/ovs_dpdk -c <core_mask> -n 4
    --proc-type=primary --base-virtaddr=<virt_addr> -- -p <port_mask>
    -n <number_of_clients> [-k <number_of_KNIs>] -m <number_of_MEMNICs>
    --stats=<stats update interval> --vswitchd=<lcore_id>
    --client_switching_core=<lcore_id> --config="<port_config>"

Sample command line to support two MEMNIC interfaces:

    # ./openvswitch/datapath/dpdk/build/ovs_dpdk -c 0xF -n 4
    --proc-type=primary --base-virtaddr=0x2aaa2aa00000
    -- -p 0x3 -n 1 -m 2 --stats=1 --vswitchd=0
    --client_switching_core=1 --config="(0,0,2),(1,0,3)"

ovs_dpdk will create shared memory in `/dev/shm`.
The above example, ovs_dpdk_48 and ovs_dpdk_49 are created.

    # ls /dev/shm
    ovs_dpdk_48 ovs_dpdk_49

#### Program the Switch's Flow Tables

The switch's flow table must be populated to allow traffic to flow to
and from a VM, via the switch and MEMNIC shared memory.
See Section 9.0 and Section 10.0 of GSG for more information.

Add flows to switch traffic appropriately:
- From ingress port to MEMNIC port used by VM
- From MEMNIC port to next target (Physical/Virtio/KNI/IVSHM/MEMNIC port)
- Any additional entries required to complete the datapath

>Note: The ID of the MEMNIC port is in the range 48-63; that is 48 = MEMNIC0,
49 = MEMNIC1 and so on.

#### Start QEMU

Start QEMU on the host

    # qemu <qemu options> -device ivshmem,size=16,shm=/ovs_dpdk_48

or use virsh

    # virsh start <domain name>

XML for this domain must have the qemu:commandline tag.

    <domain type='kvm' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>

The above option is required to use qemu:commandline tag.

    <qemu:commandline>
      <qemu:arg value='-device'/>
      <qemu:arg value='ivshmem,size=16,shm=/ovs_dpdk_48'/>
    </qemu:commandline>

The above adds MEMNIC port 0 to this VM.


### Guest Configuration

For DPDK Application, add hugepage parameter to the kernel boot
parameter in `grub.conf`.

#### Use from DPDK Application

Get the DPDK PMD driver for MEMNIC from dpdk.org.

memnic_client is a sample application.

##### Compile and Run memnic_client Sampel Application

    # ./memnic_client -c <core_mask> -n 4

The memnic_client just resend the packet which is got from the
first MEMNIC port to the same port.

#### Use from Common Socket Application

Get the MEMNIC kernel driver from dpdk.org.

    # insmod memnic.ko

Then, the network device ethX can be seen.

If the VM has 2 virtio-net and 1 MEMNIC, then `ifconfig` command
shows result like below.

    # ifconfig -a
    eth0      Link encap:Ethernet  HWaddr 52:54:00:17:F5:1B
              inet addr:10.0.2.15  Bcast:10.0.2.255  Mask:255.255.255.0
              inet6 addr: fe80::5054:ff:fe17:f51b/64 Scope:Link
              UP BROADCAST RUNNING MULTICAST  MTU:1500  Metric:1
              RX packets:287133 errors:0 dropped:0 overruns:0 frame:0
              TX packets:285804 errors:0 dropped:0 overruns:0 carrier:0
              collisions:0 txqueuelen:1000
              RX bytes:24223836 (23.1 MiB)  TX bytes:42575888 (40.6 MiB)

    eth1      Link encap:Ethernet  HWaddr 52:54:00:17:F5:1C
              BROADCAST MULTICAST  MTU:1500  Metric:1
              RX packets:0 errors:0 dropped:0 overruns:0 frame:0
              TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
              collisions:0 txqueuelen:1000
              RX bytes:0 (0.0 b)  TX bytes:0 (0.0 b)

    eth2      Link encap:Ethernet  HWaddr 72:DD:C6:65:6A:09
              BROADCAST MULTICAST  MTU:1500  Metric:1
              RX packets:0 errors:0 dropped:0 overruns:0 frame:0
              TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
              collisions:0 txqueuelen:1000
              RX bytes:0 (0.0 b)  TX bytes:0 (0.0 b)

In this case, eth0 and eth1 are virtio-net devices, and eth2 is MEMNIC device.


#### Combination of DPDK and Socket Application in Guest

When loading MEMNIC kernel driver `memnic.ko`, it probes all MEMNIC
devices. For the specific MEMNIC device would be handled by DPDK,
to unbind that device from the kernel driver.

    # echo <PCI slot> > /sys/bus/pci/drivers/memnic/unbind

Sample command line is:

    # echo 0000\:00\:04.0 > /sys/bus/pci/drivers/memnic/unbind

Device which bound to kernel driver, MEMNIC PMD would not find as a
proper MEMNIC device.
