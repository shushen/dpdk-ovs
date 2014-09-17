DPI permit to match a flow based on its application ID (skype, bittorrent,
WEBDAV, ...) instead of relying of traditional matching fiels (TCP port, ...).

______

# Implementation/usage

The application ID is stored in OF reg0 metadata matcher field. Until a flow
application ID is identified (classified), reg0=0. Then reg0=APP_ID.

```bash
ovs-ofctl add-flow br0 reg0=0x01,actions=mod_vlan_vid:1,output:17 # VoIP
ovs-ofctl add-flow br0 reg0=0x02,actions=mod_vlan_vid:2,output:17 # WEBDAV
ovs-ofctl add-flow br0 reg0=0x04,actions=drop   # skype
ovs-ofctl add-flow br0 reg0=0x05,actions=drop   # bitorrent
ovs-ofctl add-flow br0 reg0=0,action=output:17  # under classification
```

Note 1: all datapath supported actions can be used without any restriction.

Note 2: Depending of the DPI engine version or configuration, regs[] content
        and number may differ. For instance, the dummy plugin uses regs0 
        to store application ID and regs1 to store the protocol tags. Another
        DPI engine may return SIP caller ID in reg3, or whatever other 32 bits
        value. In other words: the API between the vswitch and the DPI engine
        is an array of 1 to 8 32bits registers, the interpretation of the 
        content of the registers is per DPI engine configuration/version.

ovs-vswitchd new parameters:
```bash
ovs-vswitchd <regular parameters> --dpi-engine=path-to/plugin.so,arg1,arg2,arg3,... [ --dpi-nregs=<number of registers holding the DPI result>] [ --dpi-ports=<coma separated list of ports with PDI enabled>]
```
 examples:
```bash
ovs-vswitchd -c 0x10 --proc-type=secondary -- --pidfile --dpi-engine=third-party/dpi-dummy-plugin/dummy_dpi_engine.so
```
```bash
ovs-vswitchd -c 0x10 --proc-type=secondary -- --pidfile --dpi-engine=third-party/dpi-dummy-plugin/dummy_dpi_engine.so --dpi-nregs=1 --dpi-ports=2,5
```

## The pros of this patch

* works with any datapath: DPDK, HW OF switches, Linux kernel datapath, ...
* do not affect the datapath code (so its performances)

## The cons of this patch

* until a flow is classified by the DPI engine, the flow won't be cached
  in the datapath.
* reg0 is used instead of implementing a brand new metadata matcher

______

# Test plan

## Test that the DPI plugin is **really** optional

Launch ovs-vswitch without any DPI plugin: 
```bash
ovs-vswitchd -c 0x10 --proc-type=secondary 
```
ovs-vswitchd should run normally, and log the following messages:
```bash
2014-06-25T15:13:36Z|00001|dpi|INFO|no DPI library specified
2014-06-25T15:13:36Z|00002|vswitchd|INFO|dpi global init failed
```

## Test with a wrong DPI plugin (random library)

Specify the libc as a DPI plugin:
```bash
ovs-vswitchd -c 0x10 --proc-type=secondary --dpi-engine=/lib64/libm.so.6
```
ovs-vswitchd should run normally, and log the following messages:
```bash
2014-06-25T15:19:36Z|00001|dpi|ERR|failed to open DPI library
2014-06-25T15:19:36Z|00002|vswitchd|INFO|dpi global init failed
```

## Test with the dummy DPI plugin

```bash
ovs-vswitchd -c 0x10 --proc-type=secondary -- --pidfile --dpi-engine=third-party/dpi-dummy-plugin/dummy_dpi_engine.so
```
ovs-vswitchd should run normally, and log the following messages:
```bash
2014-06-25T15:24:10Z|00001|vswitchd|INFO|dpi global init done
```
Then you should get a log for all new flows (each upcall), and the new flow
should be normally pushed in the datapath. Assuming packet injection on port 16:
```bash
ovs-ofctl del-flows ovs-br0
ovs-ofctl add-flow ovs-br0 in_port=16,action=output=17
ovs-appctl vlog/set dpi:DBG
2014-06-25T15:39:24Z|00686|dpi(miss_handler)|DBG|dpi_process: app_id=<69> tags=<0x0> <offloaded>
```
reg0 is filled with the value **69** for all new flows. All packets
injected on port 16 should be dropped (first rule match):
```bash
ovs-appctl vlog/set dpi:INFO
ovs-ofctl del-flows ovs-br0
ovs-ofctl add-flow ovs-br0 priority=100,in_port=16,reg0=69,action=drop
ovs-ofctl add-flow ovs-br0 priority=10,in_port=16,action=output=17
```
And now all packets should be forwarded on port 17 (second rule match):
```bash
ovs-ofctl del-flows ovs-br0
ovs-ofctl add-flow ovs-br0 priority=100,in_port=16,reg0=51,action=drop
ovs-ofctl add-flow ovs-br0 priority=10,in_port=16,action=output=17
```

______

# Youtube didactic videos

[usage-example-video]

[datapath-offloading-video]

Note:
    **reg0** had been renamed **axm0** when the video was taken, for didactic
    purposes (Application ID stored in an Application eXtensible Matcher,
    **axm0**, more comprehensive than than in **reg0**).

______

[usage-example-video]: http://www.youtube.com/watch?v=jkbkvX2B_kI
[datapath-offloading-video]: http://www.youtube.com/watch?v=QmnajvSsmHI
