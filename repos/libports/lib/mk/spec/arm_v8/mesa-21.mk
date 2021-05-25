SHARED_LIB := yes
LIBS       += etnaviv libdrm-etnaviv

SRC_C += gallium/winsys/etnaviv/drm/etnaviv_drm_winsys.c

CC_OPT += -DGALLIUM_ETNAVIV

include $(REP_DIR)/lib/mk/mesa-21.inc
