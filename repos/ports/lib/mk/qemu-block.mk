QEMU_CONTRIB_DIR = $(call select_from_ports,qemu)/src/qemu-3.1.0

LIBS   += libc glib zlib
CC_OPT += -Wno-unused-function

INC_DIR += $(QEMU_CONTRIB_DIR)
INC_DIR += $(QEMU_CONTRIB_DIR)/include

BLOCK_FILTER := dmg-bz2.c file-win32.c \
                gluster.c iscsi.c linux-aio.c \
                nfs.c nvme.c rbd.c ssh.c vxhs.c win32-aio.c \
                curl.c
SRC_C += $(foreach FILE,$(filter-out $(BLOCK_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/block/*.c))),block/$(FILE))

vpath %.c $(QEMU_CONTRIB_DIR)
