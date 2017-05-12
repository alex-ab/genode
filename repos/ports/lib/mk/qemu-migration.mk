QEMU_CONTRIB_DIR = $(call select_from_ports,qemu)/src/qemu-3.1.0

LIBS   += libc glib zlib
CC_OPT += -Wno-unused-function
CC_OPT += -DNEED_CPU_H

INC_DIR += $(QEMU_CONTRIB_DIR)
INC_DIR += $(QEMU_CONTRIB_DIR)/include
INC_DIR += $(QEMU_CONTRIB_DIR)/i386-softmmu
INC_DIR += $(QEMU_CONTRIB_DIR)/target/i386
INC_DIR += $(QEMU_CONTRIB_DIR)/tcg
INC_DIR += $(QEMU_CONTRIB_DIR)/tcg/i386

MIGRATION_FILTER := block.c rdma.c
SRC_C += $(foreach FILE,$(filter-out $(MIGRATION_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/migration/*.c))),migration/$(FILE))

REPLAY_FILTER :=
SRC_C += $(foreach FILE,$(filter-out $(REPLAY_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/replay/*.c))),replay/$(FILE))

vpath %.c $(QEMU_CONTRIB_DIR)
