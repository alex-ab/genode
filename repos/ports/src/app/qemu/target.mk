TARGET = qemu

QEMU_CONTRIB_DIR = $(call select_from_ports,qemu)/src/qemu-3.1.0

CC_OPT += -Wno-unused-function

SRC_CC += component.cc fixme.cc

FILTER := arch_init.c cpus.c dump.c exec.c disas.c memory.c memory_mapping.c
FILTER += monitor.c device_tree.c gdbstub.c ioport.c os-win32.c qtest.c thunk.c
FILTER += memory_ldst.inc.c qemu-bridge-helper.c qemu-keymap.c qemu-seccomp.c
FILTER += win_dump.c qemu-io.c qemu-nbd.c qemu-img.c qemu-edid.c
SRC_C += $(foreach FILE,$(filter-out $(FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/*.c))),$(FILE))

NBD_FILTER :=
SRC_C += $(foreach FILE,$(filter-out $(NBD_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/nbd/*.c))),nbd/$(FILE))

SRC_C += ui/input.c ui/input-legacy.c ui/console.c ui/qemu-pixman.c
SRC_C += ui/input-keymap.c ui/trace.c

SRC_C += chardev/char.c chardev/char-fe.c chardev/char-ringbuf.c
SRC_C += chardev/char-io.c chardev/char-mux.c

SRC_C += stubs/arch-query-cpu-model-comparison.c
SRC_C += stubs/xen-hvm.c stubs/slirp.c
SRC_C += stubs/target-get-monitor-def.c
SRC_C += stubs/arch-query-cpu-model-baseline.c

SRC_C += scsi/pr-manager-stub.c scsi/utils.c

SRC_C += audio/audio.c audio/trace.c audio/mixeng.c

SRC_C += accel/accel.c

#SRC_C += backends/cryptodev-vhost.c backends/cryptodev.c
SRC_C += backends/rng.c backends/hostmem.c backends/tpm.c

UTIL_FILTER := aio-win32.c atomic64.c compatfd.c \
               coroutine-ucontext.c coroutine-win32.c drm.c \
               event_notifier-win32.c getauxval.c \
               oslib-posix.c oslib-win32.c \
               qemu-thread-win32.c sys_membarrier.c \
               vfio-helpers.c
SRC_C += $(foreach FILE,$(filter-out $(UTIL_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/util/*.c))),util/$(FILE))

LIBS += libpixman glib libc libm zlib libc_pipe
LIBS += qemu-qapi qemu-qobject qemu-softmmu-i386 qemu-migration qemu-slirp
LIBS += qemu-qom qemu-crypto
LIBS += qemu-hw qemu-block qemu-io
LIBS += qemu-net qemu-trace

INC_DIR += $(QEMU_CONTRIB_DIR)
INC_DIR += $(QEMU_CONTRIB_DIR)/include

vpath %.c $(QEMU_CONTRIB_DIR)
