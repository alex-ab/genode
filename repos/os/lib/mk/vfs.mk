SRC_CC  += simple_env.cc
INC_DIR += $(REP_DIR)/src/lib/vfs

LIBS = base

vpath %.cc $(REP_DIR)/src/lib/vfs

SHARED_LIB = yes
