v0.8 - December 2013
--------------------
    - Added partial support for ``ovs-testsuite`` - a collection of unit tests found in standard Open vSwitch
    - Added support for Host KNI, a.k.a. KNI vEth. This, in turn, added support for [``OFTest``](http://www.projectfloodlight.org/oftest/)
    - Updated to use DPDK 1.5.2, NOTE: this update removes KNI compatibility with DPDK 1.4
    - Bug fixes

v0.7 - November 2013
--------------------
    - Datapath:
        - Added IMCP packet support
        - Added ``strip_vlan`` action support
        - Added ``mod_vlan_vid`` action support
        - Added ``mod_vlan_pcp`` action support
        - Added ``drop`` action support
        - Added support for multiple actions per flow
    - Bug fixes

v0.6 - October 2013
  -------------------
    - Datapath:
        - Added 802.1Q VLAN-tagged frame support
    - Performance Improvements
    - Bug fixes

v0.5 - August 2013
------------------
    - Datapath:
        - Added support for dynamic flow allocation using the ovs-dpctl and ovs-ofctl applications
    - Bug fixes

v0.4 - July 2013
----------------
    - Add support for numerous IO virtualization mechanisms: Virtio, IVSHM, and IVSHM with Intel(R) DPDK KNI
