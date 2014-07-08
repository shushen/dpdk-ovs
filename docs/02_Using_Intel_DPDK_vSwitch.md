Intel® DPDK vSwitch consists of a large number of interconnected utilities. Seeing as it is derived from Open vSwitch (OVS), it provides many of the utilities found in stock OVS. However, it also provides some additional utilities that are not found in stock OVS. Each of these are detailed in this section.

______

## ovs-dpdk

The `ovs-dpdk` application replaces the fastpath kernel switching module found in stock OVS with a DPDK-based userspace switching application. By building the switching logic on top of the Intel® DPDK library, packet switching throughput is significantly boosted - particularly for small packets. From an architectural perspective, `ovs-dpdk` is a datapath implementation that sits below a thin, dataplane interface (dpif) provider. This means the `ovs-dpdk` application essentially *is* Intel® DPDK vSwitch.

The `ovs-dpdk` application can be executed as follows:

```bash
./datapath/dpdk/ovs-dpdk [eal] -- [args...]
```

### Args

The Environment Abstraction Layer (EAL) arguments are required for all DPDK-enhanced applications. They consist of a number of environment specific values - what core to run on, how many threads to use etc.

Of these, the `--base_addr` argument is particularly important. The purpose of this argument is to provide the EAL with a hint of where hugepages should be mapped to. Problems have been seen where secondary processes fail due to collisions with a primary process' virtual address space. A good example of this behaviour is `ovs-dpctl` running as a secondary process. When `ovs-dpctl` starts, it loads all required shared libraries and these get mapped into their own virtual address space. Later in the execution, `ovs-dpctl` calls `rte_eal_init()` which attaches the running process to the primary process' hugepages. If these happened to be mapped to the same virtual addresses used by the shared libraries, these libraries will become unavailable due to its virtual address space being overwritten.

The process of finding a valid virtual address to use with `--base_addr` is one based on trial and error. The virtual addresses can be taken from EAL's output to `stdout`. Once a "valid" virtual address is found it can be re-used over and over with guaranties that it will work.

Check the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][dpdkorg-dpdkgsg] for more information about EAL arguments.

After the EAL arguments, the following arguments (i.e. `[args...]` above) are supported:

* `--stats`
  If zero, statistics are not displayed. If nonzero, it represents the interval in seconds at which statistics are updated onscreen

* `--stats_core`
  The ID of the core used to print statistics.

* `-p PORTMASK`
  PORTMASK Hexadecimal bitmask representing the ports to be configured, where each bit represents a port ID, that is, for a portmask of 0x3, ports 0 and 1 are configured.

### Example Command

An example configuration, with two physical ports and stats disabled:

```bash
./datapath/dpdk/ovs-dpdk -c 0x0f -n 4 -- -p 0x03 --stats_core=1 --stats=0
```

______

## ovs-ivshmem-mngr

The IVSHM manager utility is used to share the Intel® DPDK objects - created on the host by `ovs-dpdk` - with guests. It makes use of the Intel® DPDK IVSHM API. Please consult the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][dpdkorg-dpdkgsg] for details on IVSHM.

The IVSHM manager provides a flexible and easy to use interface that allows multiple combinations regarding what Intel® DPDK objects are shared with the guests. Only the port names specified in `ovs-dpdk` are needed. The utility will query the `ovs-dpdk` internal configuration and collect all Intel® DPDK ring and memzone information associated with the ports being shared. Finally, it will create the metadata and a command line to be used when running QEMU processes (i.e. starting guests). These command lines will be printed to the screen and stored in temporary files - one per guest - under the `/tmp` directory (for automation purposes).

There are a some points to note about the IVSHM manager utility. Firstly, it must always be executed after `ovs-dpdk` is up and running and all ports have been both added *and* configured. An attempt to run it before this may cause undesired behavior. Secondly, it must be run as an Intel® DPDK secondary process. Failing to do so will cause the utility to exit with an error.

### Args

The `ovs-ivshmem-mngr` application can be executed as follows:

```bash
./utilities/ovs-ivshmem-mngr [eal] -- [args...]
```

The EAL parameters are fully documented in the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][dpdkorg-dpdkgsg]. The other arguments must be a list of metadata names and port names. The metadata names are arbitrary names used to distinguish the set of Intel® DPDK objects being shared with a guest. There is a one to one relation between metadatas and guests. The port names must be the identical to those previously added to the switch. Each metadata name must be unique and followed by a comma separated list of port names. The metadata name and ports are separated by a colon (:).

Example of one metadata and four ports:

```bash
meta:port0,port1,port2,port3
```

Multiple metadatas (for multiple guests) can be specified too:

```bash
meta1:port0,port1 meta2:port2
```

In case of passing a non-valid input the IVSHM manager will fail with an error message specifying what the error was.

### Example Command

```bash
./utilities/ovs-ivshmem-mngr meta:ivshmport0,ivshmportb
```

______

## ovs-vswitchd

The Open vSwitch daemon application - `ovs-vswitchd` - "manages and controls any number of Open vSwitch switches on the local machine" [source][ovs-man-vswitchd].

### Args

The version of `ovs-vswitchd` found in Intel® DPDK vSwitch is functionally identical to that found in stock Open vSwitch with one exception - it requires EAL parameters:

```bash
./vswitchd/ovs-vswitchd [eal] -- [database]
```

The EAL parameters are fully documented in the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][dpdkorg-dpdkgsg]. For information on the remainder of the parameters/options, see the documentation for the standard `ovs-vswitchd` - available [here][ovs-man-vswitchd], in the included manpages for the application, or via the `--help` option like so:

```bash
./vswitchd/ovs-vswitchd [eal] -- --help
```

Please note that not all options provided by the utility have been validated. See *Known Issues* for a list of validated options.

### Example Command

Start `ovs-vswitchd` in verbose mode:

```bash
./vswitchd/ovs-vswitchd -c 0x10 --proc-type=secondary -- --verbose=dbg
```

**Note:** Of the available EAL parameters, the `--proc-type` is one of the most important. This specifies that the application should be run as a secondary DPDK process (where `ovs-dpdk` is the primary DPDK process). Not using this option will cause the application to fail.

______

## ovs-dpctl

The `ovs-dpctl` application is used to "create, modify, and delete Open vSwitch datapaths" [source][ovs-man-dpctl].

### Args

The version of `ovs-dpctl` found in Intel® DPDK vSwitch is functionally identical to that found in stock Open vSwitch with one exception - it requires EAL parameters:

```bash
./utilities/ovs-dpctl [eal] -- [options] command [switch] [args...]
```

The EAL parameters are fully documented in the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][dpdkorg-dpdkgsg]. For information on the remainder of the parameters/options, see the documentation for the standard `ovs-dpctl` - available [here][ovs-man-dpctl], in the included manpages for the application, or via the `--help` option like so:

```bash
./utilities/ovs-dpctl [eal] -- --help
```

Please note that not all options provided by the utility have been validated. See *Known Issues* for a list of validated options.

### Example Command

To add a new `DPDK` datapath:

```bash
./utilities/ovs-dpctl -c 11 --proc-type=secondary -- -s add-dp dpdk@dp
```

To add a new interface to the above datapath:

```bash
./utilities/ovs-dpctl -c 11 --proc-type=secondary -- -s add-if dpdk@dp
  ovsdpdk1,type=dpdkphy
```

To add a new *flow* to the above datapath, with the following spec

* Traffic: `IPv4`, `TCP`
* Source MAC: `00:00:00:00:00:01`
* Destination MAC: `00:00:00:00:00:02`
* Source IP: `1.1.1.1`
* Destination IP: `2.2.2.2`
* Input Port: `0`  (phy 1)
* Output Port: `1`  (phy 2)

```bash
./utilities/ovs-dpctl -c 11 --proc-type=secondary -- -s add-flow dpdk@dp
  "in_port(0),eth(src=00:00:00:00:00:01,dst=00:00:00:00:00:02),
  eth_type(0x0800),ipv4(src=1.1.1.1,dst=2.2.2.2,proto=6,tos=0,ttl=64,
  frag=no),tcp(src=0,dst=0)" "1"
```

______

## ovs-vsctl

The `ovs-vsctl` application "configures ovs−vswitchd by providing a high−level interface to its configuration database" [source][ovs-man-vsctl].

### Args

The version of `ovs-vsctl` found in Intel® DPDK vSwitch is the same as that found in stock Open vSwitch.

```bash
./utilities/ovs-vsctl [options] −− [options] command [args] [−− [options] command [args]]...
```

For information on the supported parameters/options, see the documentation for the standard `ovs-vsctl` - available [here][ovs-man-vsctl], in the included manpages for the application, or via the `--help` option like so:

```bash
./utilities/ovs-vsctl --help
```

Please note that not all options provided by the utility have been validated. See *Known Issues* for a list of validated options.

### Example Command

To add a new bridge to `DPDK` datapath:

```bash
./utilities/ovs-vsctl add-br br0 -- set Bridge br0 datapath_type=dpdk
```

To add a new OpenFlow port to the above bridge and request OpenFlow port number `1`:

```bash
./utilities/ovs-vsctl add-port br0 myphyport -- set Interface myphyport
  type=dpdkphy ofport_request=1
```

______

## ovs-ofctl

The `ovs-ofctl` application is used for "monitoring and administering OpenFlow switches" [source][ovs-man-ofctl].

### Args

The version of `ovs-ofctl` found in Intel® DPDK vSwitch is the same as that found in stock Open vSwitch.

```bash
./utilities/ovs-ofctl [options] command [switch] [args...]
```

For information on the supported parameters/options, see the documentation for the standard `ovs-ofctl` - available [here][ovs-man-ofctl], in the included manpages for the application, or via the `--help` option like so:

```bash
./utilities/ovs-ofctl --help
```

Please note that not all options provided by the utility have been validated. See *Known Issues* for a list of validated options.

### Example Command

Add a new *flow* to an existing bridge - `br0` - with two existing OpenFlow ports - numbers `1` and `2` - where the flow has the following spec:

* Source IP: `1.1.1.1`
* Destination IP: `2.2.2.2`
* Input Port: `1`
* Output Port: `2`

```bash
./utilities/ovs-ofctl add-flow br0 in_port=1,dl_type=0x0800,nw_src=1.1.1.1,
  nw_dst=90.90.90.90,idle_timeout=0,action=output:2
```

______

## Other Tools

All other tools provided by Open vSwitch, such as `ovsdb-tool` and `ovsdb-server` work identically in Intel® DPDK vSwitch and stock Open vSwitch.

______

© 2014, Intel Corporation. All Rights Reserved

[dpdkorg-dpdkgsg]: http://dpdk.org/doc
[ovs-man-vswitchd]: http://openvswitch.org/cgi-bin/ovsman.cgi?page=vswitchd%2Fovs-vswitchd.8
[ovs-man-dpctl]: http://openvswitch.org/cgi-bin/ovsman.cgi?page=utilities%2Fovs-dpctl.8
[ovs-man-ofctl]: http://openvswitch.org/cgi-bin/ovsman.cgi?page=utilities%2Fovs-ofctl.8
[ovs-man-vsctl]: http://openvswitch.org/cgi-bin/ovsman.cgi?page=utilities%2Fovs-vsctl.8
