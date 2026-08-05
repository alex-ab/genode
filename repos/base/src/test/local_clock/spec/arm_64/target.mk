REQUIRES = arm_64

include $(PRG_DIR)/../../target.inc

# set expected ratio of remote_clock_fn, since half of the calls are
# unsync()-ed, it cannot be lower than 0.5
CC_OPT += -DEXPECTED_RATIO=0.7

SRC_CC += sleep_or_busy_loop.cc

vpath sleep_or_busy_loop.cc $(PRG_DIR)/../arm
