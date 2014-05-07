Intel® DPDK vSwitch consists of a large number of interconnected utilities. Seeing as it is derived from Open vSwitch (OVS), it provides many of the utilities found in stock OVS. However, it also provides some additional utilities that are not found in stock OVS. Each of these are detailed in this section.

______

## ovs_dpdk

The `ovs_dpdk` application replaces the fastpath kernel switching module found in stock OVS with a DPDK-based userspace switching application. By building the switching logic on top of the Intel® DPDK library, packet switching throughput is significantly boosted - particularly for small packets. From an architectural perspective, `ovs_dpdk` is a datapath implementation that sits below a thin, dataplane interface (dpif) provider. This means the `ovs_dpdk` application essentially *is* Intel® DPDK vSwitch.

The `ovs_dpdk` application can be executed as follows:

```bash
./datapath/dpdk/build/ovs_dpdk [eal] -- [args...]
```

### Args

The Environment Abstraction Layer (EAL) arguments are required for all DPDK-enhanced applications. They consist of a number of environment specific values - what core to run on, how many threads to use etc.

Of these, the `--base_addr` argument is particularly important. The purpose of this argument is to provide the EAL with a hint of where hugepages should be mapped to. Problems have been seen where secondary processes fail due to collisions with primary processes virtual address space. A good example of this behaviour is QEMU running as a secondary process. When QEMU starts, it loads all required shared libraries and these get mapped into their own virtual address space. Later in the execution, QEMU calls `rte_eal_init()` which attaches the running process to the primary processes hugepages. If these happened to be mapped to the same virtual addresses used by the shared libraries, these libraries will become unavailable due to its virtual address space being overwritten.

The process of finding a valid virtual address to use with `--base_addr` is one based on trial and error. The virtual addresses can be taken from EAL's output to `stdout`. Once a "valid" virtual address is found it can be re-used over and over with guaranties that it will work.

Check the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][intel-dpdkgsg] for more information about EAL arguments.

After the EAL arguments, the following arguments (i.e. `[args...]` above) are supported:

* `--stats`
  If zero, statistics are not displayed. If nonzero, it represents the interval in seconds at which statistics are updated onscreen
* `--client_switching_core`
  CPU ID of the core on which the main switching loop will run
* `-p PORTMASK`
  Hexadecimal bitmask representing the ports to be configured, where each bit represents a port ID, that is, for a portmask of 0x3, ports 0 and 1 are configured
* `-n NUM`
  Number of client devices to configure
* `-k NUM`
  Number of KNI devices to configure
* `-h NUM`
  Number of Userspace-vHost devices to configure
* `-v NUM`
  Number of vEth devices to configure
* `-m NUM`
  Number of MEMNIC devices to configure
* `--config (port,queue,lcore)[,(port,queue,lcore]`
  Each port/queue/core group specifies the CPU ID of the core that will handle ingress traffic for the specified queue on the specified port
* `-J NUM`
  Maximum frame size which ovs_dpdk can handle in physical port, this will enable jumbo frame of the NIC

In addition, the following parameters are available to configure the vHost devices.

* `--vhost_dev_basename`
  Set the basename for the vhost character device. If this is not modified then the character device will default to `/dev/vhost-net`
* `--vhost_dev_index`
  Set the index to be appended to the vhost character device name. This will only be used if the basename has also been modified
* `--vhost_retry_count`
  Set the number of retries when the destination queue is full. This may need to be tuned depending on the system
* `--vhost_retry_wait`
  Wait time in uSec when the destination queue is full. This may need to be tuned depending on the system

### Example Command

An example configuration, with two physical ports, four KNI devices, and stats disabled:

```bash
./datapath/dpdk/build/ovs_dpdk -c 0x0f -n 4 -- -p 0x03 -k 4 --stats=0
  --client_switching_core=1 --config="(0,0,2),(1,0,3)"
```

______

## ovs-ivshmem-mngr

The IVSHM manager utility is used to share the Intel® DPDK objects - created on the host by `ovs_dpdk` - with guests. It makes use of the Intel® DPDK IVSHM API. Please consult the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][intel-dpdkgsg] for details on IVSHM.

The IVSHM manager provides a flexible and easy to use interface that allows multiple combinations regarding what Intel® DPDK objects are shared with the guests. Only the port names specified in `ovs_dpdk` are needed. The utility will query the `ovs_dpdk` internal configuration and collect all Intel® DPDK ring and memzone information associated with the ports being shared. Finally, it will create the metadata and a command line to be used when running QEMU processes (i.e. starting guests). These command lines will be printed to the screen and stored in temporary files - one per guest - under the `/tmp` directory (for automation purposes).

There are a some points to note about the IVSHM manager utility. Firstly, it must always be executed after `ovs_dpdk` is up and running and all ports have been both added *and* configured. An attempt to run it before this may cause undesired behavior. Secondly, it must be run as an Intel® DPDK secondary process. Failing to do so will cause the utility to exit with an error.

### Args

The `ovs-ivshmem-mngr` application can be executed as follows:

```bash
./utilities/ovs-ivshmem-mngr [eal] -- [args...]
```

The EAL parameters are fully documented in the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][intel-dpdkgsg]. The other arguments must be a list of metadata names and port names. The metadata names are arbitrary names used to distinguish the set of Intel® DPDK objects being shared with a guest. There is a one to one relation between metadatas and guests. The port names must be the identical to those previously added to the switch. Each metadata name must be unique and followed by a comma separated list of port names. The metadata name and ports are separated by a colon (:).

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
./utilities/ovs-ivshmem-mngr meta:kniport0,ivshmporta
```

______

## ovs-vswitchd

The Open vSwitch daemon application - `ovs-vswitchd` - "manages and controls any number of Open vSwitch switches on the local machine" [source][ovs-man-vswitchd].

### Args

The version of `ovs-vswitchd` found in Intel® DPDK vSwitch is functionally identical to that found in stock Open vSwitch with one exception - it requires EAL parameters:

```bash
./vswitchd/ovs-vswitchd [EAL] -- [database]
```

The EAL parameters are fully documented in the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][intel-dpdkgsg]. For information on the remainder of the parameters/options, see the documentation for the standard `ovs-vswitchd` - available [here][ovs-man-vswitchd], in the included manpages for the application, or via the `--help` option like so:

```bash
./vswitchd/ovs-vswitchd [EAL] -- --help
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
./utilities/ovs-dpctl [EAL] -- [options] command [switch] [args...]
```

The EAL parameters are fully documented in the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][intel-dpdkgsg]. For information on the remainder of the parameters/options, see the documentation for the standard `ovs-dpctl` - available [here][ovs-man-dpctl], in the included manpages for the application, or via the `--help` option like so:

```bash
./utilities/ovs-dpctl [EAL] -- --help
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
  ovs_dpdk_16,type=dpdk
```

To add a new *flow* to the above datapath, with the following spec

* Traffic: `IPv4`, `TCP`
* Source MAC: `00:00:00:00:00:01`
* Destination MAC: `00:00:00:00:00:02`
* Source IP: `1.1.1.1`
* Destination IP: `2.2.2.2`
* Input Port: `16`
* Output Port: `17`

```bash
./utilities/ovs-dpctl -c 11 --proc-type=secondary -- -s add-flow dpdk@dp
  "in_port(16),eth(src=00:00:00:00:00:01,dst=00:00:00:00:00:02),
  eth_type(0x0800),ipv4(src=1.1.1.1,dst=2.2.2.2,proto=6,tos=0,ttl=64,
  frag=no),tcp(src=0,dst=0)" "17"
```

______

## ovs-vsctl

The `ovs-vsctl` application "configures ovs−vswitchd by providing a high−level interface to its configuration database" [source][ovs-man-vsctl].

### Args

The version of `ovs-vsctl` found in Intel® DPDK vSwitch is the same as that found in stock Open vSwitch.

```bash
./utilities/ovs-ofctl [options] −− [options] command [args] [−− [options] command [args]]...
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

To add a new port to the above bridge:

```bash
./utilities/ovs-vsctl add-port br0 ovs_dpdk_16 -- set Interface ovs_dpdk_16
  type=dpdk ofport_request=16
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

Add a new *flow* to an existing bridge - `br0` - with two existing OpenFlow ports - numbers `16` and `17` - where the flow has the following spec:

* Source IP: `1.1.1.1`
* Destination IP: `2.2.2.2`
* Input Port: `16`
* Output Port: `17`

```bash
./utilities/ovs-ofctl add-flow br0 in_port=16,dl_type=0x0800,nw_src=1.1.1.1,
  nw_dst=90.90.90.90,idle_timeout=0,action=output:17
```

______

## Other Tools

All other tools provided by Open vSwitch, such as `ovsdb-tool` and `ovsdb-server` run the same in Intel® DPDK vSwitch as those in stock Open vSwitch.

______

[intel-dpdkgsg]: http://www.intel.com/content/www/us/en/intelligent-systems/
[ovs-man-vswitchd]: http://openvswitch.org/cgi-bin/ovsman.cgi?page=vswitchd%2Fovs-vswitchd.8
[ovs-man-dpctl]: http://openvswitch.org/cgi-bin/ovsman.cgi?page=utilities%2Fovs-dpctl.8
[ovs-man-ofctl]: http://openvswitch.org/cgi-bin/ovsman.cgi?page=utilities%2Fovs-ofctl.8
[ovs-man-vsctl]: http://openvswitch.org/cgi-bin/ovsman.cgi?page=utilities%2Fovs-vsctl.8
