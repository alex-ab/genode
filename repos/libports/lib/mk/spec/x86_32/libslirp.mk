include $(REP_DIR)/lib/mk/libslirp.inc

# include global.mk for definition of CUSTOM_HOST_CC
include $(BASE_DIR)/mk/global.mk

# support for multiarch environments: look for glib-2.0 arch-specific dirs
HOST_LIB_SEARCH_DIRS = $(shell $(CUSTOM_HOST_CC) $(CC_MARCH) -print-search-dirs | grep libraries |\
                               sed "s/.*=//"   | sed "s/:/ /g" |\
                               sed "s/\/ / /g" | sed "s/\/\$$//")

PKG_CONFIG_DIRS = $(wildcard $(addsuffix /pkgconfig,$(HOST_LIB_SEARCH_DIRS)))

# acquire include paths for glib-2.0 via pkg-config
INC_DIR += $(patsubst -I%,%,$(filter -I%,\
  $(shell export PKG_CONFIG_PATH=$(firstword $(PKG_CONFIG_DIRS)) && pkg-config --cflags glib-2.0)))

SRC_C += check_glib.c

vpath check_glib.c $(REP_DIR)/src/lib/libslirp/spec/x86_32
