LIBS = libc

include $(REP_DIR)/lib/mk/mesa-common-21.inc

SRC_C = $(notdir $(wildcard $(MESA_SRC_DIR)/src/gallium/drivers/softpipe/*.c))

vpath %.c $(MESA_SRC_DIR)/src/gallium/drivers/softpipe

