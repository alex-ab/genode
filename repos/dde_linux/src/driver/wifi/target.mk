REQUIRES := 64bit

TARGET  := wifi
SRC_CC  := main.cc wpa.cc access_firmware.cc
LIBS    := base wifi
LIBS    += libc
LIBS    += wpa_supplicant
LIBS    += libcrypto1 libssl1 wpa_driver_nl80211

INC_DIR += $(PRG_DIR)

CC_CXX_WARN_STRICT :=
