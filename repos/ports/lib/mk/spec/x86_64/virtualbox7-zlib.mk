include $(REP_DIR)/lib/mk/virtualbox7-common.inc

ZLIB_DIR = $(VIRTUALBOX_DIR)/src/libs/zlib-$(ZLIB_VERSION)
INC_DIR += $(ZLIB_DIR)
SRC_C    = $(notdir $(wildcard $(ZLIB_DIR)/*.c))

CC_OPT += -DHAVE_UNISTD_H=1

vpath % $(ZLIB_DIR)

CC_CXX_WARN_STRICT =
