TARGET := test-aes_cbc_4k_crypto
SRC_CC := main.cc
LIBS   += base aes_cbc_4k_crypto

vpath main.cc $(PRG_DIR)/..
