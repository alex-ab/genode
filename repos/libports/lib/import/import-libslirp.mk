LIBSLIRP_DIR := $(call select_from_ports,libslirp)
INC_DIR += $(LIBSLIRP_DIR)/include
ifneq ($(CONTRIB_DIR),)
INC_DIR += $(call select_from_repositories,src/lib/libslirp)
endif
