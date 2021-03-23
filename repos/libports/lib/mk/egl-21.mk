SHARED_LIB = yes
LIBS       = libc

include $(REP_DIR)/lib/mk/mesa-common-21.inc

SRC_C = main/eglapi.c \
        main/eglarray.c \
        main/eglconfig.c \
        main/eglcontext.c \
        main/eglcurrent.c \
        main/egldevice.c \
        main/egldisplay.c \
        main/eglglobals.c \
        main/eglimage.c \
        main/egllog.c \
        main/eglsurface.c \
        main/eglsync.c

CC_OPT += -D_EGL_NATIVE_PLATFORM=_EGL_PLATFORM_DEVICE

vpath %.c $(MESA_SRC_DIR)/src/egl

