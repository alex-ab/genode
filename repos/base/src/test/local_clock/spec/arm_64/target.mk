REQUIRES = arm_64

include $(PRG_DIR)/../../target.inc

SRC_CC += sleep_or_busy_loop.cc

vpath sleep_or_busy_loop.cc $(PRG_DIR)/../arm
