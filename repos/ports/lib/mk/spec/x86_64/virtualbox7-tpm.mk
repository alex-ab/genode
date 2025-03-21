include $(REP_DIR)/lib/mk/virtualbox7-common.inc

TPM_DIR  = $(VIRTUALBOX_DIR)/src/libs/libtpms-$(LIBTPMS_VERSION)

INC_DIR += $(TPM_DIR)
INC_DIR += $(TPM_DIR)/include
INC_DIR += $(TPM_DIR)/include/libtpms
INC_DIR += $(TPM_DIR)/src
INC_DIR += $(TPM_DIR)/src/tpm2
INC_DIR += $(TPM_DIR)/src/tpm2/crypto
INC_DIR += $(TPM_DIR)/src/tpm2/crypto/openssl

OPENSSL_DIR  = $(VIRTUALBOX_DIR)/src/libs/openssl-$(OPENSSL_VERSION)
INC_DIR += $(OPENSSL_DIR)
INC_DIR += $(OPENSSL_DIR)/include
INC_DIR += $(OPENSSL_DIR)/gen-includes

SRC_C  = $(notdir $(wildcard $(TPM_DIR)/src/*.c))
SRC_C += $(addprefix tpm12/, $(filter-out tpm_crypto_freebl.c, $(notdir $(wildcard $(TPM_DIR)/src/tpm12/*.c))))
SRC_C += $(addprefix tpm2/, $(notdir $(wildcard $(TPM_DIR)/src/tpm2/*.c)))
SRC_C += $(addprefix tpm2/crypto/openssl/, $(notdir $(wildcard $(TPM_DIR)/src/tpm2/crypto/openssl/*.c)))

SRC_CC += Devices/Security/DrvTpmEmu.cpp
SRC_CC += Devices/Security/DrvTpmEmuTpms.cpp

# see libtpms-<version>/Makefile.kmk
VBOX_CC_OPT += -include tpm_library_conf.h
VBOX_CC_OPT += -DTPM_PCCLIENT
VBOX_CC_OPT += -DTPM_VOLATILE_LOAD
VBOX_CC_OPT += -DTPM_ENABLE_ACTIVATE
VBOX_CC_OPT += -DTPM_AES
VBOX_CC_OPT += -DTPM_LIBTPMS_CALLBACKS
VBOX_CC_OPT += -DTPM_NV_DISK
VBOX_CC_OPT += -DTPM_NOMAINTENANCE_COMMANDS
VBOX_CC_OPT += -DTPM_V12
VBOX_CC_OPT += -DTPM_POSIX
VBOX_CC_OPT += -D__unix__

vpath %.c $(TPM_DIR)/src

CC_CXX_WARN_STRICT =
