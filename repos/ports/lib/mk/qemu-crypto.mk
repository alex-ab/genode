QEMU_CONTRIB_DIR = $(call select_from_ports,qemu)/src/qemu-3.1.0

LIBS   += libc glib
CC_OPT += -Wno-unused-function

INC_DIR += $(QEMU_CONTRIB_DIR)
INC_DIR += $(QEMU_CONTRIB_DIR)/include

CRYPTO_FILTER := cipher-afalg.c afalg.c cipher-builtin.c cipher-gcrypt.c
CRYPTO_FILTER += cipher-nettle.c hash-afalg.c hash-gcrypt.c hash-nettle.c
CRYPTO_FILTER += hmac-gcrypt.c hmac-nettle.c pbkdf-gcrypt.c pbkdf-nettle.c
CRYPTO_FILTER += random-gcrypt.c random-gnutls.c
SRC_C += $(foreach FILE,$(filter-out $(CRYPTO_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/crypto/*.c))),crypto/$(FILE))

vpath %.c $(QEMU_CONTRIB_DIR)
