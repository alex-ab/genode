TARGET = gears-21
LIBS   = libm libc egl-21 mesa-21

SRC_C     = eglgears.c eglut.c
SRC_CC    = eglut_genode.cc
LD_OPT    = --export-dynamic

INC_DIR  += $(REP_DIR)/src/lib/mesa-21/include \
            $(PRG_DIR)/../eglut

vpath %.c  $(PRG_DIR)/../eglut
vpath %.cc $(PRG_DIR)/../eglut

CC_CXX_WARN_STRICT =
