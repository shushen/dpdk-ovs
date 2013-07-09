Build Instructions
------------------------------

1. DPDK
cd <DPDK_DIR>
Refer to the DPDK GSG for relevant make target. eg:
make install T=x86_64-default-linuxapp-gcc

2. Openvswitch
cd <OVDK_ROOT>/openvswitch
./boot.sh
./configure RTE_SDK=<DPDK_DIR>
make

3. Qemu
cd <OVDK_ROOT>/qemu
./configure --enable-kvm --dpdkdir=<DPDK_DIR> --target-list=x86_64-softmmu
make
