REQUIRES = x86

include $(PRG_DIR)/../../target.inc

# set expected ratio of remote_clock_fn
CC_OPT += -DEXPECTED_RATIO=0.30
SRC_CC += sleep_or_busy_loop.cc
