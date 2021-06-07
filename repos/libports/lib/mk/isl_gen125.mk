LIBS = libc

include $(REP_DIR)/lib/mk/mesa-common-21.inc

CC_OPT  += -DGEN_VERSIONx10=125
include $(REP_DIR)/lib/mk/isl_gen.inc
