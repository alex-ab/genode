PLAT  := qemu-arm-virt
CPU   := cortex-a53
override BOARD := virt_qemu_arm_v8a
ARM_PLATFORM := qemu-arm-virt

-include $(REP_DIR)/lib/mk/spec/arm_v8a/kernel-sel4.inc
