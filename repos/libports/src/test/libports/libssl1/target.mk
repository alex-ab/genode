TARGET = test-libssl1
LIBS   = libc libssl1
SRC_CC = main.cc

vpath main.cc $(PRG_DIR)/..

CC_CXX_WARN_STRICT =
