REQUIRES := 64bit

TARGET  := wifi
SRC_CC  := main.cc wpa.cc access_firmware.cc
LIBS    := base wifi
LIBS    += libc
LIBS    += wpa_supplicant
LIBS    += libcrypto-minimal libssl-minimal
LIBS    += libnl wpa_driver_nl80211

INC_DIR += $(PRG_DIR)

# get access to <lx_kit/firmware.h>
INC_DIR += $(REP_DIR)/src/include

CC_CXX_WARN_STRICT :=
