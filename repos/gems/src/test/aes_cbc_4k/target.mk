TARGET := test-aes_cbc_4k
SRC_CC := main.cc hook.cc

SRC_ADS := dummy.ads
LIBS    += spark

LIBS   += base aes_cbc_4k
