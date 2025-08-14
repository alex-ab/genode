PLAT := qemu-arm-virt
ARCH := arm
BOARD := virt_qemu_arm_v8a

SEL4_ARCH := aarch64
SEL4_WORDBITS := 64

LIBS += kernel-sel4-virt_qemu_arm_v8a

include $(REP_DIR)/lib/mk/syscall-sel4.inc
