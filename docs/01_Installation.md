Intel® DPDK vSwitch consists of multiple components. Each of these must be downloaded and compiled in order to run any of the sample configurations detailed later.

______

## Build Requirements

To compile Intel® DPDK vSwitch, you will need the following software:

* `gcc`
* `make`
* GNU Autotools, i.e. `autoconf`, `autom4te`, `automake`
* `kernel-devel.$(uname -r)`
* `kernel`
* `autoconf`
* `automake`
* `zlib-devel`
* `libtool`
* `glib-devel`
* `glib2-devel`
* `expect`
* `fuse`
* `fuse-devel`
* `openssl`
* `kernel-modules-extra`

______

## Download

### Intel® DPDK

Intel® DPDK is a set of software libraries and Ethernet drivers (native and virtualized) that run in Linux user space to boost packet processing throughput on Intel® Architecture. You can find information, including download links, for Intel® DPDK via the Intel® DPDK portal on [01.org][01org-dpdk].

**Note:** Intel® DPDK vSwitch does not currently support the version of Intel® DPDK provided by `dpdk.org`. Please use only the latest versions from [intel.com][intel-dpdk] or [01.org][01org-dpdk].

### Intel® DPDK vSwitch

Intel® DPDK vSwitch (along with supporting applications) can be sourced in three different ways:

* By cloning or downloading this repository
* By downloading the latest stable release from [01.org][01org-downloads]

This guide will assume the latter option has been chosen: please adjust the provided commands accordingly.

### OFTest (Optional)

OFTest is a test framework designed to "exercise a candidate OpenFlow switch (the device/switch under test, DUT or SUT)". It is an optional component that can be used to evaluate the functionality of Intel® DPDK vSwitch. You can find information, including download links, for OFTest on the [OFTest GitHub page][oftest-github].

OFTest requires a number of additional utilities to be installed prior to use - refer to the *Pre-requisites* section of the [Project Floodlight Getting Started Guide][oft-gsg]

______

## Extract

Before compiling, extract the source files for Intel® DPDK, Intel® DPDK vSwitch and (optionally) OFTest. This can be done as follows:

```bash
mkdir ~/ovs_dpdk
cd ~/ovs_dpdk
mkdir ovdk
unzip <dpdk_release_pkg>.tar.gz
tar -xvzg <dpdk_vswitch_release_pkg>.tar.gz  -C ovdk
mv <dpdk_extract_dir> DPDK
```

If you intend to test OpenFlow compliance with OFTest, you should also extract this now.

______

## Compile

There are two approaches possible for compiling Intel® DPDK vSwitch, its supporting applications and its dependencies: *automatically*, via the provided top-level makefile, or manually.

### Top-level Makefile

Intel® DPDK vSwitch provides a top-level makefile capable of building each component of Intel in the correct order and fashion. This makefile provides a number of targets:

* `all`
* `config`
* `clean`
* `check`
* `xxx`, where `xxx` is one of the following: `ovs`, `dpdk`, `qemu`
* `xxx-deps`, where `xxx` is one of the following: `ovs`, `qemu`, `ivshm`, `kni`
* `config-xxx`, where `xxx` is one of the following: `ovs`, `dpdk`, `qemu`
* `clean-xxx`, where `xxx` is one of the following: `ovs`, `dpdk`, `qemu`

A typical execution of this would look like:

```bash
cd ~/ovs_dpdk               # directory containing DPDK and DPDK vSwitch
export RTE_SDK=$(pwd)/DPDK  # DPDK sub-directory
cd ovdk                     # DPDK vSwitch sub-directory
make config && make
```

**Note:** The top-level makefile Intel® DPDK vSwitch has been validated in a limited number of environments, and may be subject to portability issues. Please follow the manual steps below if you encounter such issues.

### Manually

Three different components are necessary to run Intel® DPDK vSwitch: Intel® DPDK, QEMU and the applications of Intel® DPDK vSwitch. Of these, Intel® DPDK must be built first due to dependencies in the latter applications.

Each of these commands should be run from the top-level directory in which you previously extracted all dependencies, i.e.:

```bash
cd ~/ovs_dpdk               # directory containing DPDK and DPDK vSwitch
```

#### DPDK

Refer to the [Intel® DPDK Getting Started Guide][intel-dpdkgsg] for a relevant make target, e.g.:

```bash
cd DPDK                     # DPDK sub-directory
export RTE_SDK=$(pwd)
export RTE_TARGET="x86_64-ivshmem-linuxapp-gcc"
make install T="$RTE_TARGET"
cd -
```

**Note:** To enable the new Intel® DPDK 1.6.0 IVSHM feature, Intel® DPDK has to be compiled using the `ivshmem` target. This requirement may differ depending on the environment in which the system is built. Please refer to sections two and three of the *Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide* - which can be found on [intel.com][intel-dpdkgsg] - for additional information on performing this step.

#### Open vSwitch

```bash
cd ovdk/openvswitch         # DPDK vSwitch sub-directory
./boot.sh
./configure RTE_SDK=$(RTE_SDK) --disable-ssl
make
cd -
```

#### QEMU

```bash
cd ovdk/qemu
./configure --enable-kvm --dpdk-dir=$(RTE_SDK) --target-list=x86_64-softmmu --disable-pie
cd -
```

______

## Environment Setup

Following successful compilation, a number of final steps may be necessary in order to run Intel® DPDK vSwitch. Each of these steps *may not be necessary*, and should be run/skipped accordingly. Additionally, many of these commands may need to be executed each time the board is rebooted. Therefore, please keep this section a *reference guide*.

Please note, that as with the previous section, each of these commands should be run from the top-level directory in which you previously extracted all dependencies, i.e.:

```bash
cd ~/ovs_dpdk
```

### Configure Kernel Boot Parameters

Intel® DPDK vSwitch requires hugepages be enabled. Multiple 1 GB and 2 MB hugepages are supported; however, standard Intel® DPDK vSwitch configuration requires a minimum of one 1 GB hugepage per instance of the vSwitch. This can be enabled by adding the following to the kernel boot parameters:

```bash
default_hugepagesz=1G hugepagesz=1G hugepages=1
```

Userspace-vHost ports may require multiple hugepages. For example, if using a VM with 2 GB of memory and giving the switch 2 GB memory, we need to allocate eight hugepages. This ensures that we have the four hugepages required on the same socket. To enable these eight 1 GB hugepages:

```bash
default_hugepagesz=1G hugepagesz=1G hugepages=8
```

2 MB pages may also be used:

```bash
default_hugepagesz=2M hugepagesz=2M hugepages=2048
```

Following this, it will be necessary to reboot your device. Please do so before continuing.

Please note that Intel® DPDK vSwitch requires hugepages in order to run. If your device does not support these, Intel® DPDK vSwitch will be unable to execute. You can check support using the [Intel® ARK Tool][intel-ark]

### Create Required Directories

You may need to manually create some directories if you have not previously run either Intel® DPDK vSwitch or stock Open vSwitch, or have deleted prior versions of the directories:

```bash
mkdir -p /usr/local/etc/openvswitch
mkdir -p /usr/local/var/run/openvswitch
```

### Mount Huge Pages

Hugepages may or may not be mounted on your system. You can check this as follows:

```bash
mount | grep /dev/hugepages
```

If this returns no results, mount the hugepages like so:

```bash
mkdir -p /dev/hugepages
mount -t hugetlbfs nodev /dev/hugepages
```

### Insert `igb_uio` Driver & Bind PCI Devices

While Intel® DPDK vSwitch provides a large number of different paths to guests, it still requires physical Network Interface Cards (NICs), bound to the `igb_uio` driver, to function correctly. You can check if this driver is already loaded as follows:

```bash
lsmod | grep igb_uio
```

If this returns no results, insert the driver like so:

```bash
modprobe uio
insmod ./DPDK/$RTE_TARGET/kmod/igb_uio.ko
```

Once the `igb_uio` driver has been loaded, some interfaces should be bound to it. Intel® DPDK does not do this automatically so you should the `pci_unbind.py` script found in `$RTE_SDK/tools/`. In brief, the following commands can be used:

To get the current status of the ports (i.e. what's bound and what isn't):

```bash
./DPDK/tools/pci_unbind.py --status
```

To bind devices:

```bash
./DPDK/tools/pci_unbind.py --bind igb_uio [hw_addr...]
```

For example:

```bash
./DPDK/tools/pci_unbind.py --bind igb_uio 81:00.0 81:00.1
```

You can refer to the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][intel-dpdkgsg] - for full instructions on performing this step.

______

[01org-downloads]: https://01.org/packet-processing/downloads
[01org-dpdk]: https://01.org/packet-processing/overview/dpdk-detail
[oftest-github]: https://github.com/floodlight/oftest
[intel-dpdk]: http://www.intel.com/content/www/us/en/intelligent-systems/intel-technology/packet-processing-is-enhanced-with-software-from-intel-dpdk.html
[intel-dpdkgsg]: http://www.intel.com/content/www/us/en/intelligent-systems/intel-technology/intel-dpdk-getting-started-guide.html
[intel-ark]: http://ark.intel.com/
[oft-gsg]: http://docs.projectfloodlight.org/display/OFTest/Longer+Start
