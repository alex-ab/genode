LIBSSL_PORT_DIR := $(call select_from_ports,openssl-minimal)

LIBS    += libcrypto-minimal
SRC_CC  += aes_cbc_4k.cc

INC_DIR += $(REP_DIR)/src/lib/aes_cbc_4k
INC_DIR += $(LIBSSL_PORT_DIR)/include

vpath % $(REP_DIR)/src/lib/aes_cbc_4k
