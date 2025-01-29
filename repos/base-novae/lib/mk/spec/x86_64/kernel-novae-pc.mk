REQUIRES = novae

NOVAE_DIR := $(call select_from_ports,novae)/src/kernel/novae

#
# Execute the kernel build only at the second build stage when we know
# about the complete build settings (e.g., the 'CROSS_DEV_PREFIX') and the
# current working directory is the library location.
#
ifeq ($(called_from_lib_mk),yes)
all: build_kernel
else
all:
endif

build_kernel:
	$(VERBOSE) make ARCH=x86_64 -s -C $(NOVAE_DIR) \
	                            BLD_DIR=$(PWD)/kernel/novae \
	                            PREFIX_x86_64=$(CROSS_DEV_PREFIX)
