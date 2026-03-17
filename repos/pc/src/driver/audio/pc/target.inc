REQUIRES := x86_64

REL_PRG_DIR := $(PRG_DIR)/../..

TARGET := pc_audio
LIBS   := base pc_lx_emul jitterentropy vfs

INC_DIR += $(REL_PRG_DIR) $(REL_PRG_DIR)/shadow
INC_DIR += $(LX_SRC_DIR)/sound/hda/core
INC_DIR += $(LX_SRC_DIR)/sound/hda/common
INC_DIR += $(LX_SRC_DIR)/sound/hda/controllers

SRC_CC += audio.cc
SRC_CC += firmware.cc
SRC_CC += main.cc
SRC_CC += misc.cc
SRC_CC += lx_emul/acpi.cc
SRC_C  += $(notdir $(wildcard $(REL_PRG_DIR)/generated_dummies.c))
SRC_C  += acpi.c
SRC_C  += acpixf.c
SRC_C  += common_dummies.c
SRC_C  += dummies.c
SRC_C  += lx_emul.c
SRC_C  += lx_user.c
SRC_C  += timeout.c
SRC_C  += lx_emul_firmware.c

# linux/efi.h
CC_OPT_sound/hda/codecs/side-codecs/cs35l56_hda     += -Wno-incompatible-pointer-types
CC_OPT_sound/hda/codecs/side-codecs/cs35l56_hda_spi += -Wno-incompatible-pointer-types
CC_OPT_sound/soc/codecs/cs-amp-lib                  += -Wno-incompatible-pointer-types
CC_OPT_sound/soc/codecs/cs35l56-shared              += -Wno-incompatible-pointer-types
CC_OPT_dummies                                      += -Wno-incompatible-pointer-types

CC_OPT_shadow/drivers/gpio/gpiolib-acpi-core += -I$(LX_SRC_DIR)/drivers/gpio
SRC_C  += shadow/drivers/gpio/gpiolib-acpi-core.c

# pull in 'dde_linux' acpi emulation rather than the stub one in 'pc'
DDE_LINUX_ACPI := 1

SRC_C   += lx_emul/shadow/drivers/acpi/bus.c
SRC_C   += lx_emul/shadow/drivers/acpi/device_sysfs.c
SRC_C   += lx_emul/shadow/drivers/acpi/glue.c
SRC_C   += lx_emul/shadow/drivers/acpi/property.c
SRC_C   += lx_emul/shadow/drivers/acpi/scan.c
SRC_C   += lx_emul/shadow/drivers/acpi/utils.c

DDE_LINUX_DIR := $(subst /src/include/lx_kit,,$(call select_from_repositories,src/include/lx_kit))
vpath % $(DDE_LINUX_DIR)/src/lib

#
# Original symbol is renamed to __real_* and references to original symbol
# are replaced by __wrap_*. Used to shadow 'usleep_range_state', see
# timeout.c
#
LD_OPT += --wrap=usleep_range_state

vpath %.c  $(REP_DIR)/src/lib/pc
vpath %.cc $(REP_DIR)/src/lib/pc
vpath %.c  $(REL_PRG_DIR)
vpath %.cc $(REL_PRG_DIR)
vpath common_dummies.c $(REP_DIR)/src/lib/pc/lx_emul
vpath %.s16 $(REL_PRG_DIR)
