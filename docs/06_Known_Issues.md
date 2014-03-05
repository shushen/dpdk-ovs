There are a small number of long-term issues, related to both the general design of Intel® DPDK vSwitch and outstanding bugs. These are detailed below.

______

## Intel® DPDK vSwitch

* This release supports Intel® DPDK v1.6.0 only. Intel® DPDK v1.5.2 is no longer supported.

* Intel® Virtualization Technology for Directed I/O (Intel® VT-d) should be disabled in the BIOS settings, unless PCI passthrough is required, in which case the following options should be added to the kernel boot parameters:

    ```
    intel_iommu=on iommu=pt
    ```

* Memory corruption is possible if the cores specified using the `-c` option overlap between processes.

* When starting the VMs, the following warning may appear:

    ```bash
    (ASLR) is enabled in the kernel. This may cause issues with mapping memory into secondary processes.
    ```

    Although in most cases this warning is harmless, to suppress it, run the following command:

    ```bash
    echo 0 > /proc/sys/kernel/randomize_va_space
    ```

* Only one instance of the `kni_client` application should be started in a guest; however, to create multiple KNI devices in a single VM, use the `-p` parameter specifying the KNI ports to initialize and connect to. For example, to connect to KNI ports `KNI0` and `KNI1` in the VM (see KNI section for further details):

    ```bash
    ./kni_client -c 0x1 -n 4 -- -p KNI0 -p KNI1
    ```

* In Intel® DPDK vSwitch, packet data is copied before it is injected into VirtIO, which may introduce a higher packet drop rate with larger packet sizes. In general, speeds for VirtIO are similar to standard QEMU, if slightly lower; currently, ways to improve the performance with a different design are being investigated. KNI is offered as a backwards-compatible alternative to VirtIO (that is, it supports non-Intel® DPDK userspace applications in the guest), and offers significantly better performance compared to VirtIO. Intel recommends this option when high throughput is required in a non-Intel® DPDK application use case.

* This release has not been tested or validated for use with Virtual Functions, although it should theoretically work with Intel® DPDK 1.6.0.

* If testing performance with TCP, variances in performance may be observed; this variation is due to the protocol's congestion-control mechanisms. UDP produces more reliable and repeatable results, and it is the preferred protocol for performance testing.

* On start-up, Intel® DPDK vSwitch may issue an error:

    ```bash
    EAL: memzone_reserve_aligned_thread_unsafe(): memzone
    <RG_MP_log_history> already exists
    RING: Cannot reserve memory
    ```

    When an Intel® DPDK process starts, it attempts to reserve memory for various rings through a call to `rte_memzone_reserve`; in the case of a Intel® DPDK primary process, the operation should succeed, but for a secondary process, it is expected to fail, as the memory has already been reserved by the primary process. The particular ring specified in the error message - `RG_MP_log_history` - does not affect operation of the secondary process, so this error may be disregarded.

* On start-up, ovs_dpdk may complain that no ports are available (when using an Intel® DPDK-supported NIC):

    ```bash
    Total ports: 0

    EAL: Error - exiting with code: 1
    Cause: Cannot allocate memory for port tx_q details
    ```

    These error messages indicate that Intel® DPDK initialization failed because it did not detect any recognized physical ports. One possible cause is that the NIC is still driven by the default ixgbe driver. To resolve this issue, run `DPDK/tools/pci_unbind.py` before starting ovs-dpdk. (This process lets the Intel® DPDK poll mode driver take over the NIC.)

    For example, `pci_unbind.py -b igb_uio <PCI ID of NIC port>` binds the NIC to the Intel® DPDK igb_uio driver.

* As `ovs_dpdk` requires modification to achieve compatibility with 82571EB-based dual-port cards, modify `openvswitch/datapath/dpdk/init.c`, updating the value of `tx_rings` in the `init_port` function from `num_clients` to `1`, and recompile.

* Passing a VLAN packet with VLAN ID `0`, but a priority greater than `0` (a priority tagged packet) is not currently supported, and passing this type of packet will render the switch unresponsive.

______

## Intel® DPDK vSwitch Sample Guest Application

In the current IVSHM implementation, multiple Intel® DPDK objects (memory zones, rings, and memory pools) can be shared between different guests. The host application determines what to share with each guest. The guest applications are not Intel® DPDK secondary processes anymore, and so they can create their own Intel® DPDK objects as well.

______

## Open vSwitch Testsuite

Open vSwitch contains a number of unit tests that collectively form the OVS "testsuite". While the majority of these tests currently pass without issue, a small number do fail. The common cause of failure for these tests is a discrepancy in the command line arguments required by many of the utilities in standard Open vSwitch and their equivalents in Intel® DPDK vSwitch. In addition, test three (3) causes the testsuite to hang and should be skipped. These issues will be resolved in a future release.

Many of the tests also fail due to differences in the required parameters for utilities such as `ovs-dpctl` (that is, Intel® DPDK vSwitch's version of these utilities require EAL parameters). As a result, these tests should be used as guidelines only.

In addition to the standard unit tests, Intel® DPDK vSwitch extends the testsuite with a number of "Intel® DPDK vSwitch"-specific unit tests. These tests require root privileges to run, due to the use of hugepages by the Intel® DPDK library. These tests are currently the only tests guaranteed to pass.

______

## OFTest

Adding a route when using virtual Ethernet devices has been known to cause system instability. The root cause of this issue is currently being investigated.

A number of OFTest tests currently fail. In most cases, this failure is due to missing functionality in either standard Open vSwitch or Intel® DPDK vSwitch. These issues will be resolved as additional functionality is added. A full list containing the current status of the tests is given in Section 12.3.

OFTest has been validated agains Scapy v2.2.

______

## QEMU

The IVSHM model has been validated only on QEMU v1.4.0 and above. This limitation is due to a known bug in earlier versions (such as v1.1), which prevents mapping of hugepages of size > 256 MB. Further modifications were added to allow multiple IVSHM files to be passed into the command line.

QEMU is added as an Intel® DPDK secondary process, attempting to run a secondary process before a primary process will result in a segfault. This behavior is standard for Intel® DPDK applications.

______

## ovs-vswitchd

Not all functionality that is supported by Open vSwitch is supported by the Intel® DPDK vSwitch.

______

## ovs-vsctl

Not all functionality that is supported by Open vSwitch is supported by the Intel® DPDK vSwitch.

______

## ovs-ofctl

Not all functionality that is supported by Open vSwitch is supported by the Intel® DPDK vSwitch.

______

## ovs-dpctl

Not all functionality that is supported by Open vSwitch is supported by the Intel® DPDK vSwitch.

______

## ovs-ivshm-mngr

The IVSHM manager utility must be executed once the switch is up and running and not before. An attempt to share Intel® DPDK objects using the IVSHM manager utility before the switch has finished with its setup/init process may cause undesired behavior.

______
