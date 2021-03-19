MESA_PORT_DIR := $(call select_from_ports,mesa-21)
MESA_SRC_DIR  := $(MESA_PORT_DIR)/src/lib/mesa
LIB_DIR       := $(REP_DIR)/src/lib/mesa

INC_DIR += $(MESA_PORT_DIR)/include \
           $(MESA_PORT_DIR)/include/nir \
           $(MESA_PORT_DIR)/include/compiler \
           $(MESA_PORT_DIR)/include/compiler/nir

CC_OPT += -DHAVE_PTHREAD \
          -DHAVE_TIMESPEC_GET

# vi: set ft=make :
