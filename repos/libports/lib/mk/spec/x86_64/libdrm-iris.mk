include $(REP_DIR)/lib/mk/libdrm.inc

include $(call select_from_repositories,lib/import/import-libdrm-iris.mk)

SRC_CC := ioctl_iris.cc

#vpath %.c  $(DRM_SRC_DIR)
#vpath %.c  $(LIB_DIR)
#vpath %.cc $(LIB_DIR)
