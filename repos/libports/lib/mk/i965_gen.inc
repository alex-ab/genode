LIBS = libc

include $(REP_DIR)/lib/mk/mesa-common-21.inc

INC_DIR += $(MESA_GEN_DIR)/src/compiler/nir \
           $(MESA_GEN_DIR)/src/intel \
           $(MESA_SRC_DIR)/src/intel \
           $(MESA_SRC_DIR)/src/mapi \
           $(MESA_SRC_DIR)/src/mesa \
           $(MESA_SRC_DIR)/src/mesa/drivers/dri/i965 \
           $(MESA_SRC_DIR)/src/mesa/drivers/dri/common

#INC_DIR += $(MESA_GEN_DIR)/src/compiler/nir \
#           $(MESA_GEN_DIR)/src/intel \
#           $(MESA_SRC_DIR)/src/compiler/nir \
#           $(MESA_SRC_DIR)/src/gallium/auxiliary \
#           $(MESA_SRC_DIR)/src/intel \
#           $(MESA_SRC_DIR)/src/mapi \
#           $(MESA_SRC_DIR)/src/mesa

CC_OPT  += -DGALLIUM_I965

SRC_C = drivers/dri/i965/genX_blorp_exec.c \
        drivers/dri/i965/genX_pipe_control.c \
        drivers/dri/i965/genX_state_upload.c

vpath %.c $(MESA_SRC_DIR)/src/mesa
