SCFW_PORT = $(call select_from_ports,imx8-scfw)
SCFW_DIR  = $(SCFW_PORT)/src/lib/imx8-scfw

INC_DIR += $(SCFW_DIR)/arch/arm/include
INC_DIR += $(call select_from_repositories,src/lib/imx8-scfw)

SRC_C  = svc/pm/rpc_clnt.c
SRC_C += svc/pad/rpc_clnt.c
SRC_C += svc/irq/rpc_clnt.c
SRC_C += svc/rm/rpc_clnt.c

SRC_CC = ipc.cc

vpath %.c $(SCFW_DIR)/arch/arm/mach-imx/sci
vpath %.cc $(call select_from_repositories,src/lib/imx8-scfw)
