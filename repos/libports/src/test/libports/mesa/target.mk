TARGET = test-mesa-minimal
LIBS   = libc mesa-minimal egl-minimal
SRC_CC = main.cc

vpath main.cc $(PRG_DIR)/..

CC_CXX_WARN_STRICT =
