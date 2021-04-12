DRM_SRC_DIR := $(call select_from_ports,libdrm-105)/src/lib/libdrm
INC_DIR     += $(DRM_SRC_DIR) $(addprefix $(DRM_SRC_DIR)/,include/drm include)
