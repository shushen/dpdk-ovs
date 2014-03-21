Intel® DPDK vSwitch provides a number of reference applications for guests. These can be found in the `guest` subdirectory of the Intel® DPDK vSwitch package. Each application is detailed in this section.

______

## kni_client

The `kni_client` application creates a DPDK KNI (Kernel NIC Inteface) device in the guest, which communicates with KNI ports on the switch (ovs_dpdk) via shared DPDK rings.

The `kni_client` application can be executed as follows:

```bash
./kni_client [eal] -- -p <KNI port name> [-p <KNI port name>...]
```

### Args

The Environment Abstraction Layer (EAL) arguments are required for all DPDK-enhanced applications. They consist of a number of environment-specific values: what cores to run on, how many threads to use etc. They are fully documented in the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][intel-dpdkgsg].

Apart from the EAL arguments, the following application-specific arguments are supported:

* `-p <KNI port name> [-p <KNI port name>]`
  A list of KNI ports to bind to

### Example Command

To "bind" to two KNI ports, called `kniport0` and `kniport1`, respectively:

```bash
./kni_client –c 0xf -n 4 -- -p kniport0 -p kniport1
```

______

## ovs_client

`ovs_client` is a simple reference application which demonstrates the performance benefits of IVSHM-based I/O.  Each instance of the application running within a guest connects to a `CLIENT` port on the switch, and simply returns any traffic it receives from that port to the switch.

The `ovs_client` application can be executed as follows:

```bash
./ovs_client [eal] -- -p <client port name>
```

### Args

The EAL parameters are fully documented in the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][intel-dpdkgsg].

After the EAL arguments, the following application-specific arguments are supported:

* `-p <client port name>`
  Name of a `CLIENT` port to bind to

### Example Command

To "bind" to an IVSHM port called `clientport0`:

```bash
./ovs_client –c 0xf -n 4 -- -p clientport0
```

______

[intel-dpdkgsg]: https://www-ssl.intel.com/content/www/us/en/intelligent-systems/intel-technology/packet-processing-is-enhanced-with-software-from-intel-dpdk.html
