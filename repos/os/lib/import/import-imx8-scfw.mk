INC_BASE_DIR_SCFW = $(call select_from_ports,imx8-scfw)/src/lib/imx8-scfw
INC_DIR += $(INC_BASE_DIR_SCFW)/arch/arm/include
INC_DIR += $(INC_BASE_DIR_SCFW)/arch/arm/include/asm/mach-imx
INC_DIR += $(INC_BASE_DIR_SCFW)/arch/arm/mach-imx
INC_DIR += $(call select_from_repositories,src/lib/imx8-scfw)
