
LIBS = libc libdrm \
       i965_gen80 i965_gen90 i965_gen110 \
       isl_gen80  isl_gen90  isl_gen110  isl_gen120  isl_gen125

LIBS += expat zlib

include $(REP_DIR)/lib/mk/mesa-common-21.inc

CC_CXX_WARN_STRICT =

CC_OPT   += -DGALLIUM_I965
# We rename 'ioctl' calls to 'genode_ioctl' calls (drm lib)
CC_C_OPT += -Dioctl=genode_ioctl

INC_DIR += $(MESA_GEN_DIR)/src/compiler \
           $(MESA_GEN_DIR)/src/compiler/nir \
           $(MESA_GEN_DIR)/src/intel

INC_DIR += $(MESA_SRC_DIR)/src/compiler/nir \
           $(MESA_SRC_DIR)/src/intel \
           $(MESA_SRC_DIR)/src/mapi \
           $(MESA_SRC_DIR)/src/mesa \
           $(MESA_SRC_DIR)/src/mesa/main \
           $(MESA_SRC_DIR)/src/mesa/drivers/dri/i965 \
           $(MESA_SRC_DIR)/src/mesa/drivers/dri/common

FILTER_OUT_GENX = genX_pipe_control.c genX_blorp_exec.c genX_state_upload.c
SRC_C += $(addprefix mesa/drivers/dri/i965/, $(filter-out $(FILTER_OUT_GENX), $(notdir $(wildcard $(MESA_SRC_DIR)/src/mesa/drivers/dri/i965/*.c))))

SRC_C += $(addprefix mesa/tnl/, $(notdir $(wildcard $(MESA_SRC_DIR)/src/mesa/tnl/*.c)))

SRC_C  += $(addprefix intel/compiler/, $(notdir $(wildcard $(MESA_SRC_DIR)/src/intel/compiler/*.c)))
SRC_CC += $(addprefix intel/compiler/, $(notdir $(wildcard $(MESA_SRC_DIR)/src/intel/compiler/*.cpp)))

SRC_C += $(addprefix intel/perf/, $(notdir $(wildcard $(MESA_SRC_DIR)/src/intel/perf/*.c)))
SRC_C += intel/perf/gen_perf_metrics.c

SRC_CC += mesa/drivers/dri/i965/brw_link.cpp \
          mesa/drivers/dri/i965/brw_nir_uniforms.cpp

SRC_C  += mesa/main/texformat.c

SRC_C  += $(addprefix mesa/swrast/, $(notdir $(wildcard $(MESA_SRC_DIR)/src/mesa/swrast/*.c)))
SRC_C  += mesa/swrast_setup/ss_context.c \
          mesa/swrast_setup/ss_triangle.c

SRC_C  += $(addprefix intel/blorp/, $(notdir $(wildcard $(MESA_SRC_DIR)/src/intel/blorp/*.c)))

SRC_C  += $(addprefix intel/common/, $(notdir $(wildcard $(MESA_SRC_DIR)/src/intel/common/*.c)))

SRC_C  += intel/dev/gen_debug.c \
          intel/dev/gen_device_info.c

SRC_C += intel/isl/isl.c \
         intel/isl/isl_aux_info.c \
         intel/isl/isl_gen7.c \
         intel/isl/isl_gen8.c \
         intel/isl/isl_gen9.c \
         intel/isl/isl_gen12.c \
         intel/isl/isl_drm.c \
         intel/isl/isl_format.c \
         intel/isl/isl_format_layout.c \
         intel/isl/isl_tiled_memcpy_normal.c

SRC_CC += i965/unsupported.cc

vpath %.c   $(MESA_SRC_DIR)/src
vpath %.c   $(MESA_GEN_DIR)/src
vpath %.c   $(REP_DIR)/src/lib/mesa-21
vpath %.cpp $(MESA_SRC_DIR)/src
vpath %.cc  $(REP_DIR)/src/lib/mesa-21
