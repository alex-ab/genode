QEMU_CONTRIB_DIR = $(call select_from_ports,qemu)/src/qemu-3.1.0

LIBS   += libc glib libpixman zlib

CC_OPT += -Wno-unused-function

INC_DIR += $(QEMU_CONTRIB_DIR)
INC_DIR += $(QEMU_CONTRIB_DIR)/include

# XXX better way ?
#CC_OPT  += -D__FreeBSD__

INC_DIR += $(REP_DIR)/src/app/qemu/include
INC_DIR += $(QEMU_CONTRIB_DIR)/i386-softmmu

#CC_OPT  += -iquote $(QEMU_CONTRIB_DIR)/target/i386
CC_OPT  += -DNEED_CPU_H

INC_DIR += $(QEMU_CONTRIB_DIR)/target/i386
INC_DIR += $(QEMU_CONTRIB_DIR)/tcg
INC_DIR += $(QEMU_CONTRIB_DIR)/tcg/i386
INC_DIR += $(QEMU_CONTRIB_DIR)/capstone/include
INC_DIR += $(QEMU_CONTRIB_DIR)/accel/tcg

TCG_FILTER := user-exec.c user-exec-stub.c
SRC_C += $(foreach FILE,$(filter-out $(TCG_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/accel/tcg/*.c))),accel/tcg/$(FILE))

I386_FILTER := hax-all.c hax-darwin.c hax-mem.c hax-windows.c hyperv.c kvm.c sev.c
I386_FILTER += whpx-all.c
SRC_C += $(foreach FILE,$(filter-out $(I386_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/target/i386/*.c))),target/i386/$(FILE))

SRC_C += arch_init.c cpus.c dump.c exec.c ioport.c qtest.c thunk.c

SRC_C += tcg/tcg.c tcg/tcg-op.c tcg/tcg-op-vec.c tcg/tcg-op-gvec.c
SRC_C += tcg/tcg-common.c tcg/optimize.c

SRC_C += accel/stubs/kvm-stub.c

# SRC_C += tcg/tci.c

SRC_C += disas/tci.c
SRC_C += disas/i386.c
SRC_C += disas.c

SRC_C += fpu/softfloat.c

SRC_C += memory.c
SRC_C += memory_mapping.c
SRC_C += monitor.c

SRC_C += capstone/cs.c capstone/utils.c capstone/MCInst.c capstone/SStream.c

SRC_C += i386-softmmu/trace/generated-helpers.c

vpath %.c $(QEMU_CONTRIB_DIR)
