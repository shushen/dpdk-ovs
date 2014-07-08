#   BSD LICENSE
#
#   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#Intel DPDK OVS Makefile

#DPDK Target Setup #################
#Use default target unless one is specified from the command line.
RTE_TARGET := x86_64-ivshmem-linuxapp-gcc
ifdef T
ifeq ("$(origin T)", "command line")
RTE_TARGET := $(T)
endif
endif

export RTE_TARGET
#End DPDK Target Setup #############

#DPDK Location Setup #################
#User must define location of DPDK repo
ifndef RTE_SDK
$(error RTE_SDK undefined!)
endif
#End DPDK Location Setup #############

export NUMPROC=$(shell cat /proc/cpuinfo | grep processor | wc -l)

#Directories########################
export ROOT_DIR := $(CURDIR)
export DPDK_DIR := $(RTE_SDK)
export DOC_DIR := $(ROOT_DIR)/docs
export OVS_DIR := $(ROOT_DIR)/openvswitch
export QEMU_DIR := $(ROOT_DIR)/qemu-1_4_0
export IVSHM_DIR := $(ROOT_DIR)/guest/ovs_client
export MEMNIC_PATCH := $(ROOT_DIR)/guest/memnic/memnic_set_mac_address.patch
#End Directories####################

#WRL Build Variables################
ifndef CC
	$(warning WARNING: Need to export CC on Wind River Linux machines)
endif
#End WRL Build Variables############

#Targets with Dependencies##########
.PHONY: all ivshm-deps ovs-deps qemu-deps
all: dpdk ovs-deps qemu-deps ivshm-deps

ovs-deps: dpdk config-ovs
	cd $(OVS_DIR) && $(MAKE) -j $(NUMPROC) && cd $(ROOT_DIR)

qemu-deps: dpdk config-qemu
	cd $(QEMU_DIR) && $(MAKE) -j $(NUMPROC) && cd $(ROOT_DIR)

ivshm-deps: dpdk
	cd $(IVSHM_DIR) && $(MAKE) -j $(NUMPROC) && cd $(ROOT_DIR)

#End Targets with Dependencies######

#Targets for Configuration###########
#These do not include a make and can therefore be used with tools.
.PHONY: config patch-memnic clean-patch-memnic config-dpdk config-ovs config-qemu
config: config-dpdk config-ovs config-qemu

config-dpdk: patch-memnic
	cd $(DPDK_DIR) && CC=$(CC) EXTRA_CFLAGS=-fPIC $(MAKE) -j $(NUMPROC) config T=$(RTE_TARGET) && cd $(ROOT_DIR)

config-ovs:
	cd $(OVS_DIR) && ./boot.sh && ./configure RTE_SDK=$(DPDK_DIR) --disable-ssl && cd $(ROOT_DIR)

config-qemu:
	cd $(QEMU_DIR) && ./configure --enable-kvm --dpdkdir=$(DPDK_DIR) --target-list=x86_64-softmmu && cd $(ROOT_DIR)
#End Targets for Configuration#######

#Targets for Clean##################
.PHONY: clean clean-qemu clean-ivshm clean-ovs clean-dpdk clean-patch-memnic
clean: config-dpdk clean-ivshm clean-ovs clean-qemu clean-patch-memnic clean-dpdk

clean-dpdk:
	cd $(DPDK_DIR) && $(MAKE) clean && cd $(ROOT_DIR)

clean-qemu:
	cd $(QEMU_DIR) && $(MAKE) clean && cd $(ROOT_DIR)

clean-ovs:
	cd $(OVS_DIR) && $(MAKE) clean && cd $(ROOT_DIR)

clean-ivshm:
	cd $(IVSHM_DIR) && $(MAKE) clean && cd $(ROOT_DIR)

#End Targets for Clean##############

#Targets for Check##################
.PHONY: check check-ovs
check: check-ovs

check-ovs:
	cd $(OVS_DIR) && $(MAKE) check TESTSUITEFLAGS='1329-' && cd $(ROOT_DIR)

#End Targets for Check##############

#Simple Targets#####################
.PHONY: dpdk ivshm ovs qemu docs

dpdk: config-dpdk
	cd $(DPDK_DIR) && CC=$(CC) EXTRA_CFLAGS=-fPIC $(MAKE) -j $(NUMPROC) install T=$(RTE_TARGET) && cd $(ROOT_DIR)

ovs:
	cd $(OVS_DIR) && $(MAKE) && cd $(ROOT_DIR)

qemu:
	cd $(QEMU_DIR) && $(MAKE) && cd $(ROOT_DIR)

ivshm: ovs
	cd $(IVSHM_DIR) && $(MAKE) && cd $(ROOT_DIR)

docs:
	$(MAKE) -C $(DOC_DIR)
#End Simple Targets#################

#MEMNIC patching#######################
patch-memnic:
	@if patch -N -p1 -d $(MEMNIC_DIR) --dry-run --silent <$(MEMNIC_PATCH) 2>&1 >/dev/null; then \
		echo "Applying MEMNIC patch"; \
		patch -N -p1 -d $(MEMNIC_DIR) <$(MEMNIC_PATCH); \
	else \
		echo "MEMNIC patch doesn't apply. If the patch was already applied ignore this message."; \
	fi

clean-patch-memnic:
	@if patch -R -N -p1 -d $(MEMNIC_DIR) --dry-run --silent <$(MEMNIC_PATCH) 2>&1 >/dev/null; then \
		echo "Reversing MEMNIC patch"; \
		patch -R -N -p1 -d $(MEMNIC_DIR) <$(MEMNIC_PATCH); \
	else \
		echo "MEMNIC patch doesn't reverse. If the patch was not already applied ignore this message."; \
	fi
#End MEMNIC patching###################
