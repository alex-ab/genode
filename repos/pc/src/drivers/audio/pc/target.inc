REQUIRES := x86

REL_PRG_DIR := $(PRG_DIR)/../..

TARGET := pc_audio_drv
LIBS   := base pc_lx_emul jitterentropy

INC_DIR += $(REL_PRG_DIR) $(REL_PRG_DIR)/shadow

SRC_CC += audio.cc
SRC_CC += firmware.cc
SRC_CC += main.cc
SRC_CC += misc.cc
SRC_CC += lx_vmap.cc
SRC_C  += $(notdir $(wildcard $(REL_PRG_DIR)/generated_dummies.c))
SRC_C  += acpi.c
SRC_C  += common_dummies.c
SRC_C  += dummies.c
SRC_C  += lx_emul.c
SRC_C  += lx_user.c
SRC_C  += timeout.c

SRC_BIN += nhlt-fuji5c.dat

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
vpath %.dat $(REL_PRG_DIR)/acpi
vpath %.s16 $(REL_PRG_DIR)