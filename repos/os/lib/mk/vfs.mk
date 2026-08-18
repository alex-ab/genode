SRC_CC  += root.cc
INC_DIR += $(REP_DIR)/src/lib/vfs

LIBS = base

vpath %.cc $(REP_DIR)/src/lib/vfs

SHARED_LIB = yes
