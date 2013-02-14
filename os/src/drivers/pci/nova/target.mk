TARGET   = pci_device_pd
SRC_CC   = main.cc
LIBS     = env

REQUIRES = nova
 
LD_TEXT_ADDR = 0x00001000
CXX_LINK_OPT += -Wl,--entry=activation_entry
