ifeq ($(CONTRIB_DIR),)
DRM_SRC_DIR := $(realpath $(call select_from_repositories,include/libdrm)/../..)
else
DRM_SRC_DIR := $(call select_from_ports,libdrm)/src/lib/libdrm
endif

INC_DIR += $(DRM_SRC_DIR)
INC_DIR += $(DRM_SRC_DIR)/include
INC_DIR += $(addprefix $(DRM_SRC_DIR)/,include/libdrm include/drm include)
INC_DIR += $(addprefix $(DRM_SRC_DIR)/,include/etnaviv include)
INC_DIR += $(addprefix $(DRM_SRC_DIR)/,etnaviv)
INC_DIR += $(addprefix $(DRM_SRC_DIR)/,iris)
