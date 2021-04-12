LIB_DIR := $(REP_DIR)/src/lib/libdrm

# include before to shadow libdrm_macros.h
INC_DIR += $(LIB_DIR)/include

include $(call select_from_repositories,lib/import/import-libdrm.inc)

LIBS        := libc
SHARED_LIB  := yes

SRC_C := xf86drm.c \
         xf86drmHash.c \
         xf86drmMode.c \
         xf86drmRandom.c \
         xf86drmSL.c

SRC_C += dummies.c

CC_OPT = -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1 -DHAVE_SYS_SYSCTL_H=1

#
# We rename 'ioctl' calls to 'genode_ioctl' calls, this way we are not required
# to write a libc plugin for libdrm.
#
CC_C_OPT += -Dioctl=genode_ioctl

vpath %.c  $(DRM_SRC_DIR)
vpath %.c  $(LIB_DIR)
vpath %.cc $(LIB_DIR)

CC_CXX_WARN_STRICE :=
