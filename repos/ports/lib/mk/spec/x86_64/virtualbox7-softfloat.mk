include $(REP_DIR)/lib/mk/virtualbox7-common.inc

SOFTFLOAT_DIR = $(VIRTUALBOX_DIR)/src/libs/softfloat-3e

SRC_C += $(notdir $(wildcard $(SOFTFLOAT_DIR)/source/*.c))
SRC_C += $(addprefix 8086/,$(notdir $(wildcard $(SOFTFLOAT_DIR)/source/8086/*.c)))

INC_DIR += $(SOFTFLOAT_DIR)/source/include
INC_DIR += $(SOFTFLOAT_DIR)/source/8086
INC_DIR += $(SOFTFLOAT_DIR)/build/vbox

FILTERED_OUT_SRC_C += s_addExtF80M.c
FILTERED_OUT_SRC_C += s_addF128M.c
FILTERED_OUT_SRC_C += s_compareNonnormExtF80M.c
FILTERED_OUT_SRC_C += s_invalidF128M.c
FILTERED_OUT_SRC_C += s_mulAddF128M.c
FILTERED_OUT_SRC_C += s_normRoundPackMToExtF80M.c
FILTERED_OUT_SRC_C += s_normRoundPackMToF128M.c
FILTERED_OUT_SRC_C += s_normSubnormalF128SigM.c
FILTERED_OUT_SRC_C += s_roundPackMToExtF80M.c
FILTERED_OUT_SRC_C += s_roundPackMToF128M.c
FILTERED_OUT_SRC_C += s_shiftLeftM.c
FILTERED_OUT_SRC_C += s_shiftNormSigF128M.c
FILTERED_OUT_SRC_C += s_shiftRightJamM.c
FILTERED_OUT_SRC_C += s_shiftRightM.c
FILTERED_OUT_SRC_C += s_tryPropagateNaNExtF80M.c
FILTERED_OUT_SRC_C += s_tryPropagateNaNF128M.c
FILTERED_OUT_SRC_C += 8086/s_commonNaNToF128M.c
FILTERED_OUT_SRC_C += 8086/s_f128MToCommonNaN.c
FILTERED_OUT_SRC_C += 8086/s_propagateNaNF128M.c

SRC_C := $(filter-out $(FILTERED_OUT_SRC_C), $(SRC_C))

vpath % $(SOFTFLOAT_DIR)/source

CC_CXX_WARN_STRICT =
