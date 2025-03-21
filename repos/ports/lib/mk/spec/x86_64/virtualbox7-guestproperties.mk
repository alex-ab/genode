include $(REP_DIR)/lib/mk/virtualbox7-common.inc

SHARED_LIB = yes

SRC_CC += VBoxGuestPropSvc.cpp

LIBS += stdcxx

vpath %.cpp $(VBOX_DIR)/HostServices/GuestProperties
