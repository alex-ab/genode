include $(GENODE_DIR)/repos/base/recipes/src/base_content.inc

content: include/os/attached_mmio.h

include/%.h:
	mkdir -p $(dir $@)
	cp $(GENODE_DIR)/repos/os/$@ $@

content: README
README:
	cp $(REP_DIR)/recipes/src/base-sel4-virt_qemu_arm_v8a/README $@

content: lib/import etc include/sel4
lib/import etc include/sel4:
	$(mirror_from_rep_dir)

content: src/tool/sel4_tools
src/kernel:
	$(mirror_from_rep_dir)

KERNEL_PORT_DIR := $(call port_dir,$(REP_DIR)/ports/sel4)

src/kernel/sel4: src/kernel
	cp -r $(KERNEL_PORT_DIR)/src/kernel/sel4/* $@

ELFLOADER_PORT_DIR := $(call port_dir,$(REP_DIR)/ports/sel4_tools)
src/tool/sel4_tools: src/kernel/sel4
	mkdir -p $@
	cp -r $(ELFLOADER_PORT_DIR)/src/tool/sel4_tools/* $@

content: etc/board.conf

etc/board.conf:
	echo "BOARD = virt_qemu_arm_v8a" > etc/board.conf

content:
	mv lib/mk/spec/arm_v8a/ld-sel4.mk lib/mk/spec/arm_v8a/ld.mk
	cp -r $(GENODE_DIR)/repos/base-sel4/src/timer/dummy src/timer/dummy
	sed -i "s/sel4_dummy_timer/timer/" src/timer/dummy/target.mk
	find lib/mk/spec -name kernel-sel4-*.mk -o -name syscall-sel4-*.mk |\
		grep -v "sel4-virt_qemu_arm_v8a.mk" | xargs rm -rf

