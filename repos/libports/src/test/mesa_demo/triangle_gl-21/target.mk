TARGET = triangle_gl-21
LIBS   = libm libc egl-21 mesa-21

SRC_C  = eglut.c main.c
SRC_CC = eglut_genode.cc
LD_OPT = --export-dynamic

EGLUT_DIR = $(PRG_DIR)/../eglut

INC_DIR += $(REP_DIR)/src/lib/mesa-21/include \
           $(EGLUT_DIR)

vpath %.c  $(EGLUT_DIR)
vpath %.cc $(EGLUT_DIR)


CC_CXX_WARN_STRICT =
