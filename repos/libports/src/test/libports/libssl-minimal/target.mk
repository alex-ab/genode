TARGET = test-libssl-minimal
LIBS   = libc libssl-minimal
SRC_CC = main.cc

vpath main.cc $(PRG_DIR)/..

CC_CXX_WARN_STRICT =
