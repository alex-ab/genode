MESA_INC_DIR := $(call select_from_ports,mesa-minimal)
INC_DIR += $(MESA_INC_DIR)/include
INC_DIR += $(call select_from_repositories,include/drm-uapi)
