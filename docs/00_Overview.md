[Intel® DPDK vSwitch][dpdk-ovs-github] is a fork of the open source Open vSwitch multilayer virtual switch found at [openvswitch.org][ovs].

Intel and Wind River teams re-created the kernel forwarding module (data plane) by building the switching logic on top of the Intel® DPDK library to significantly boost packet switching throughput. The Forwarding engine incorporates Intel® DPDK Huge Page Tables. The Open vSwitch control daemon is modified to connect to Intel® DPDK Huge Page Tables. The forwarding module runs in Linux user space with BSD license rights. Intel® DPDK vSwitch implements a subset of the switching functionality of Open vSwitch.

______

## Introduction

Intel® DPDK vSwitch is a proofpoint Virtual Switch implemented using Intel® DPDK acceleration and Open vSwitch. The Virtual Switch is a key technology in realizing Network Function Virtualization. The aim of this proofpoint is to significantly improve the performance of Open vSwitch while maintaining its core functionality.

[QEMU][qemu] is a generic and open source machine emulator and virtualizer. The QEMU source code has been modified and provided here to enable efficient inter-VM communications by interfacing with an accelerated OpenVSwitch and improving small-packet performance.

[Open vSwitch][ovs] is a production quality, multilayer virtual switch licensed under the open source Apache 2.0 license. The Open vSwitch source code has been modified and provided here to enable fast packet switching on Intel® Architecture and improve small-packet performance.

```
 +-------------+           +-------------+           +-------------+           +---------------------+
 | Guest       |           | Guest       |           | Guest       |           | Guest               |
 |             |           |             |           |   +------+  |           |                     |
 |             |           |             |           |   |Intel®|  |           |            +------+ |
 | +---------+ |           | +---------+ |           |   |DPDK  |  |           | +-------+  | PMD  | |
 |U| Linux   | |           |U| Linux   | |           |U  +---^--+  |           |U|Linux  |  +--^---+ |
 |-| Sockets |-|           |-| Sockets |-|           |------||-----|           |-|Sockets|----||-----|
 |K+---^-----+ |           |K+-----^---+ |           |K     ||     |           |K+-----^-+    ||     |
 |     ||      |           |      ||     |           | +----v----+ |           |      ||      ||     |
 | +----v----+ |           | +----v----+ |           | | |Bar 2| | |           | +----v----+  ||     |
 | | KNI     | |           | | Virtio  | |           | | +-----+ | |           | | Virtio  |  ||     |
 | +---^-----+ |           | +-----^---+ |           | | IVSHMEM | |           | +-----^---+  ||     |
 |     ||      |           |      ||     |           | +----^----+ |           |      ||      ||     |
 | +-+--v--+-+ |           |      ||     |           |      ||     |           |      ||      ||     |
 | | |Bar 2| | |           |      ||     |           |      ||     |           |      ||      ||     |
 | | +-----+ | |           |      ||     |           |      ||     |           |      ||      ||     |
 | | IVSHMEM | |           |      ||     |           |      ||     |           |      ||      ||     |
 | +----^----+ |           |      ||     |           |      ||     |           |      ||      ||     |
 +------||-----+           +------||-----+           +------||-----+           +------||------||-----+
        ||                        ||                        ||                        ||      ||
 +------||-----+           +------||-----+           +------||-----+           +------||------||-----+
 | QEMU ||     |           | QEMU ||     |           |QEMU  ||     |           |QEMU  ||      ||     |
 | +-----v---+ |           | +----v----+ |           | +-----v---+ |           |      ||      ||     |
 | | IVSHMEM | |           | | Virtio  | |           | | IVSHMEM | |           |      ||      ||     |
 | +----^----+ |           | |         | |           | +----^----+ |           |      ||      ||     |
 |      ||     |           | | +------+| |           |      ||     |           |      ||      ||     |
 |      ||     |           | | |Intel®|| |           |      ||     |           |      ||      ||     |
 |      ||     |           | | |DPDK  || |           |      ||     |           |      ||      ||     |
 |      ||     |           | +-+---^--++ |           |      ||     |           |      ||      ||     |
 +-------------+           +------||-----+           +------||-----+           +------||------||-----+
        ||         +--------------+|                        ||                        ||      ||
        ||         |+--------------+                        ||                        ||------+|
        ||         ||          +----------------------------+|                        ||-------+
        ||         ||          |+----------------------------+                        ||
        ||         ||          ||                                                     ||
 +------||---------||----------||----------------------------+                        ||
 |Intel® DPDK      ||          ||                            |                        ||
 |vSwitch Forwarding|          ||        +--------------------------------------------+|
 |ApplicationForwarding        ||        |+--------------------------------------------+
 |Path  ||         ||          ||        ||                  |
 |      ||         ||          ||        ||                  |
 |+-----||---------||----------||--------||-----------------+|
 ||Forwarding path ||          ||        ||                 ||
 ||     ||         ||          ||        ||                 || +---------------+
 ||     ||         ||          ||        ||                 || |Open vSwitch   |
 || +----v---+ +---v----+ +-----v--+ +----v---+ +--------+  || |Daemon         |
 || |Virtual | |Virtual | |Virtual | |Virtual | |Virtual |  || |+-------------+|
 || |Ethernet| |Ethernet| |Ethernet| |Ethernet| |Ethernet<------|Intel® DPDK  ||
 || |Port    | |Port    | |Port    | |Port    | |Port    |------>Control      ||
 || +----^---+ +----^---+ +----^---+ +----^---+ +----^---+  || ||Interface    ||
 ||     ||         ||         ||         ||         ||      || ||             ||
 || +---v----------v----------v----------v----------v-----+ || |+-------------+|
 || |Packet Forwarding                                    | || +---------------+
 || +---------------^----------^--------------------------+ ||
 ||                ||         ||                            ||
 ||            +---v----+ +---v----+                        ||
 ||            |Ethernet| |Ethernet|                        ||
 ||            |Port    | |Port    |                        ||
 ||            +-----^--+ +--^-----+                        ||
 |+------------------||-----||------------------------------+|
 +-------------------||-----||-------------------------------+
                     ||     ||
                     ||     ||
 +--------------------v-----v--------------------+
 |Intel® DPDK PMD Drivers                        |
 +------------------------^----------------------+
                         ||
                         ||                                                           Host User Space
 +-----------------------||-------------------------------------------------------------------------+
                         ||                                                         Host Kernel Space
                         ||
                +--------v--------+
                | Physical NICs   |
                +-----------------+
```

____

## Features

### Inter Virtual Machine Communication Methods

Intel® DPDK vSwitch currently provides four methods of communicating from guest-to-host.

#### KNI (Kernel Network Interface)

**Suggested use case:** Virtual Appliance running Linux with a Legacy Sockets based application. KNI offers superior performance to Virtio (see below) for Socket Based applications.

* Zero copy between guest and host below the sockets layer
* Option when applications are trusted and Intel® DPDK not used in Guest
* Opportunity to add VM to VM security through additional buffer allocation (via. `memcpy`)

#### VirtIO

**Suggested use case:** Virtual Appliance running standard Virtio drivers.

* Similar performance to standard Virtio
* Option when a vSwitch is required in conjunction with Linux or non-Linux guest OS (e.g. Windows\*, VxWorks\*, ..)

#### IVSHMEM

**Suggested use case:** Virtual Appliance running Linux with an Intel® DPDK based application.

* Zero copy between guest and switch
* Option when applications are trusted and highest small packet throughput required.
* Option when applications do not need the Linux network stack
* Opportunity to add VM to VM security through additional buffer allocation (via. `memcpy`)

#### Userspace vHost

**Suggested use cases:** Virtual Appliance running Linux with an Intel® DPDK based application, or a legacy VirtIO based application.

* Option when applications are not trusted and highest small packet throughput required.
* Option when applications either do or do not need the Linux network stack.
* Single memcpy between guest and switch provides security.
* Single memcpy for guest to guest provides security.
* Option when using a modified QEMU version is not possible.

#### MEMNIC

**Suggested use cases:** Virtual Appliance running Linux with a Legacy Sockets based application.

* Option when a vSwitch is required in conjunction with Linux or non-Linux guest OS (e.g. Windows\*, VxWorks\*, ..)
* Single memcpy between guest and switch provides security.
* Single memcpy for guest to guest provides security.
* Option when using a modified QEMU version is not possible.

### Open vSwitch Datapath Features

Only a subset of the OpenFlow actions implemented in the original Open Source Open vSwitch application are currently supported by Intel® DPDK vSwitch.

Currently, the following are supported:

* `output`
* `drop`
* `set ethernet`
* `set IPv4`
* `set TCP`
* `set UDP`
* VLAN actions
  - `strip_vlan`
  - `mod_vlan_vid`
  - `mod_vlan_pcp`

Intel® DPDK vSwitch supports action sets.

______

## Code Modifications

### QEMU

A number of modifications have been made to QEMU 1.4.0

* A modified VirtIO back-end has been provided. This allows a QEMU virtual machine to interface directly with the accelerated Open vSwitch without any transition to host userspace. This will allow legacy applications that depend on standard socket interfaces in the guest to interface with the accelerated Open vSwitch. This code can be found in `qemu/net/dpdk*`
* A modified Inter-VM Shared Memory PCI device allows for an Intel® DPDK hugepage to be shared with an Intel® DPDK application in the guest. This allows for very fast zero-copy communication with the accelerated OpenVSwitch. This code can be found in `qemu/hw/ivshmem.c`
* As KNI uses the Inter-VM Shared Memory PCI device, no changes are required to be made to QEMU to enable KNI

### Open vSwitch

Open vSwitch 2.0 has been modified adding a user-space dpif provider accelerated by the Intel® DPDK. This code is provided in `openvswitch/datapath/dpdk/`.

______

## Guest Applications

* `ovs_client` application: Sample application to demonstrate ivshmem method. This code is provided in `guest/ivshm`
* `kni_client` application: Sample application to demonstrate KNI method. This code is provided in `guest/kni/kni_client`
* `kni_misc.patch`: Patch to apply to Intel® DPDK to enable KNI method. This code is provided in `guest/kni`

More detailed information can be found in the [Sample Applications][doc-sample-apps] section.

______

## Some Notes on this Getting Started Guide

There are some things to note about this getting started guide:

* All code samples and commands have been manually wrapped at < 80 characters. This means they won't execute correctly if copied and pasted. Remove these line breaks, where they exist, before execution.
* Most, if not all, of the shell commands given within this guide require root access. Please execute as root or via sudo.

______

[ovs]: http://openvswitch.org/
[dpdk-ovs-github]: http://github.com/01org/dpdk-ovs
[qemu]: http://wiki.qemu.org/Main_Page
[doc-sample-apps]: 03_Sample_Applications.md
