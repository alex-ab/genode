TARGET = test-libcrypto-minimal
LIBS   = libc libcrypto-minimal
SRC_CC = main.cc

vpath main.cc $(PRG_DIR)/..

CC_CXX_WARN_STRICT =
