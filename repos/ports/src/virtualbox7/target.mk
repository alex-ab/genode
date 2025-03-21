REQUIRES = x86_64

TARGET = virtualbox7

included_from_target_mk := yes
include $(REP_DIR)/lib/mk/virtualbox7-common.inc

CC_WARN += -Wall

SRC_CC := main.cc drivers.cc glx_x11.cc
SRC_CC += libc.cc unimpl.cc dummies.cc pdm.cc devices.cc nem.cc
SRC_CC += pthread.cc network.cc devxhci.cc
SRC_CC += sup.cc sup_sem.cc sup_gmm.cc sup_drv.cc sup_vm.cc sup_vcpu.cc sup_gim.cc
SRC_CC += HostServices/common/message.cpp services/services.cc
SRC_CC += call_func.cc
SRC_CC += vusb_root_helper.cc

# VBOX_COM_INPROC required for DrvReg registration in NvramStoreImpl.h
CC_OPT_drivers = -DVBOX_COM_INPROC -DNvramStore=ClientNvramStore

LIBS  += base
LIBS  += stdcxx
LIBS  += libiconv
LIBS  += qemu-usb libyuv
LIBS  += mesa

CC_OPT_main = -Wno-multistatement-macros
CC_OPT += -DProgress=ClientProgress

LIBS += virtualbox7-dis
LIBS += virtualbox7-sup
LIBS += virtualbox7-devices
LIBS += virtualbox7-vmm
LIBS += virtualbox7-main
LIBS += virtualbox7-xpcom
LIBS += virtualbox7-liblzf
LIBS += virtualbox7-xml
LIBS += virtualbox7-bios
LIBS += virtualbox7-zlib
LIBS += virtualbox7-storage
LIBS += virtualbox7-runtime
LIBS += virtualbox7-apiwrap
LIBS += virtualbox7-client
LIBS += virtualbox7-softfloat
LIBS += virtualbox7-tpm
LIBS += virtualbox7-openssl

INC_DIR += $(call select_from_repositories,src/lib/libc)
INC_DIR += $(call select_from_repositories,src/lib/libc)/spec/x86_64

INC_DIR += $(REP_DIR)/src/virtualbox7
INC_DIR += $(VBOX_DIR)/HostDrivers/Support
INC_DIR += $(VBOX_DIR)/Main/include
INC_DIR += $(VBOX_DIR)/Main/src-server
INC_DIR += $(VBOX_DIR)/Main/xml
INC_DIR += $(VBOX_DIR)/NetworkServices
INC_DIR += $(VBOX_DIR)/Runtime/include
INC_DIR += $(VBOX_DIR)/VMM/include
INC_DIR += $(VIRTUALBOX_DIR)/VBoxAPIWrap
INC_DIR += $(VIRTUALBOX_DIR)/include/VBox/Graphics

# search path to 'scan_code_set_1.h'
INC_DIR += $(call select_from_repositories,src/driver/ps2)

# export VirtualBox symbols to shared objects (e.g., VBoxSharedClipboard.so)
LD_OPT = --export-dynamic

LIBS += blit

vpath call_func.cc $(call select_from_repositories,src/lib/libc)/spec/x86_64
vpath %.cc $(REP_DIR)/src/virtualbox7/

CC_CXX_WARN_STRICT =
