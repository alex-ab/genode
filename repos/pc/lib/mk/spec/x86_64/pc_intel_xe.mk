DRIVER_LIB := yes
SHARED_LIB := yes

TARGET_LIB_DIR := $(REP_DIR)/src/lib/pc_intel_xe

INC_DIR += $(TARGET_LIB_DIR)

SRC_C = lx_xe_brightness.c

vpath lx_xe_brightness.c $(TARGET_LIB_DIR)
vpath %.c $(REP_DIR)/src/lib/pc

LIBS += pc_linux_generated pc_lx_emul
LIBS += jitterentropy

INC_DIR += $(LX_SRC_DIR)/drivers/gpu/drm/xe/display/ext
INC_DIR += $(LX_SRC_DIR)/drivers/gpu/drm/xe/compat-i915-headers
INC_DIR += $(LX_SRC_DIR)/drivers/gpu/drm/i915/display
INC_DIR += $(LX_SRC_DIR)/drivers/gpu/drm/xe
INC_DIR += $(LX_SRC_DIR)/drivers/gpu/drm/i915

#
# -DCONFIG_DRM_XE_DISPLAY required because of
#  "depends on DRM_XE && DRM_XE=m"
# in linux/drivers/gpu/drm/xe/Kconfig
#
#  "-Ddrm_i915_private=xe_device" hackery
# due to linux/drivers/gpu/drm/xe/Makefile
#
CC_C_OPT += -DCONFIG_DRM_XE_DISPLAY \
            -Ddrm_i915_private=xe_device \
            -DCONFIG_ARCH_FORCE_MAX_ORDER=14

LD_OPT += -Bsymbolic
