QEMU_CONTRIB_DIR = $(call select_from_ports,qemu)/src/qemu-3.1.0

LIBS   += libc glib
CC_OPT += -Wno-unused-function

INC_DIR += $(QEMU_CONTRIB_DIR)
INC_DIR += $(QEMU_CONTRIB_DIR)/include

IO_FILTER :=
SRC_C += $(foreach FILE,$(filter-out $(IO_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/io/*.c))),io/$(FILE))

vpath %.c $(QEMU_CONTRIB_DIR)
