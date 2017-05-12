QEMU_CONTRIB_DIR = $(call select_from_ports,qemu)/src/qemu-3.1.0

LIBS   += libc glib
CC_OPT += -Wno-unused-function

INC_DIR += $(QEMU_CONTRIB_DIR)
INC_DIR += $(QEMU_CONTRIB_DIR)/include

NET_FILTER := l2tpv3.c netmap.c tap-linux.c tap-solaris.c tap-bsd.c tap-win32.c
NET_FILTER += vde.c
SRC_C += $(foreach FILE,$(filter-out $(NET_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/net/*.c))),net/$(FILE))

vpath %.c $(QEMU_CONTRIB_DIR)
