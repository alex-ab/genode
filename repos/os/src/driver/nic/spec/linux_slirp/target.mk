TARGET   = linux_slirp_nic
REQUIRES = linux
LIBS     = lx_hybrid nic_driver net libslirp
INC_DIR += $(PRG_DIR)
SRC_CC   = main.cc
LX_LIBS += glib-2.0
