INC_DIR += $(REP_DIR)/src/core/spec/virt_qemu
INC_DIR += $(REP_DIR)/src/core/spec/arm_v8
INC_DIR += $(REP_DIR)/src/core/spec/arm_gic

# add C++ sources
SRC_CC += platform_services.cc
SRC_CC += kernel/vm_thread_off.cc
SRC_CC += kernel/cpu_up.cc
SRC_CC += kernel/lock.cc
SRC_CC += spec/arm_v8/cpu.cc
SRC_CC += spec/arm_v8/kernel/thread.cc
SRC_CC += spec/arm_v8/kernel/cpu.cc
SRC_CC += spec/arm/platform_support.cc
SRC_CC += spec/arm_gic/pic.cc
SRC_CC += spec/virt_qemu/timer.cc
SRC_CC += spec/64bit/memory_map.cc

#add assembly sources
SRC_S += spec/arm_v8/exception_vector.s
SRC_S += spec/arm_v8/crt0.s

vpath spec/64bit/memory_map.cc $(BASE_DIR)/../base-hw/src/lib/hw

# include less specific configuration
include $(REP_DIR)/lib/mk/core-hw.inc
