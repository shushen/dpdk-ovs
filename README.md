# Intel® DPDK vSwitch

## What is Intel® DPDK vSwitch?

The Intel® DPDK Accelerated Open vSwitch (Intel® DPDK vSwitch) is a fork of Open vSwitch, the Open Source multilayer virtual switch found at [openvswitch.org].

For more information on the project, check out the Intel® DPDK vSwitch homepage at [01.org].

______

## Getting Started

To get started right away, we recommend that you check out the documentation contained in the `docs` directory. These files provide an in-depth overview of the components, system requirements and basic operation of Intel(R) DPDK vSwitch.  The documents are written in Markdown format, so for best results, view the documentation using a Markdown viewer, or on [GitHub].

______

## Requirements

Intel® DPDK vSwitch has been validated against the following Linux distributions:

* Intel® DPDK 1.6
* Wind River Linux 5.1
* Fedora 16

Additionally, Intel® DPDK vSwitch has been validated on the following processors:

* Intel® Xeon® Processor E5 Family
* Intel® Atom® Processor C2000 Family

______

## Build Instructions

Three different utilities are necessary to build Open vSwitch: Intel® DPDK, QEMU and Open vSwitch. Of these, Intel® DPDK must be built first due to dependencies in Intel® DPDK vSwitch.

* DPDK

    Refer to the Intel® DPDK [Getting Started Guide] for a relevant make target, eg:

    ```bash
    cd $(DPDK_DIR)
    make install T=x86_64-ivshmem-linuxapp-gcc
    ```

* Openvswitch

    ```bash
    cd $(OVS_DIR)/openvswitch
    ./boot.sh
    ./configure RTE_SDK=$(DPDK_DIR)
    make
    ```

*  QEMU

    ```bash
    cd $(OVS_DIR)/qemu
    ./configure --enable-kvm --dpdkdir=$(DPDK_DIR) --target-list=x86_64-softmmu
    make
    ```

______

## Further Information

For further information, please check out the `docs` directory, or use the mailing list.

______

## Contribute

Please submit all questions, bugs and patch requests to the official [mailing list]. For further information on this process, please refer to the ``CONTRIBUTE`` file.

______

[01.org]: https://01.org/packet-processing/intel%C2%AE-ovdk
[openvswitch.org]: http://openvswitch.org
[GitHub]:  https://github.com/01org/dpdk-ovs/tree/master/docs
[mailing list]: https://lists.01.org/mailman/listinfo/dpdk-ovs
[Getting Started Guide]: http://dpdk.org/doc
