include $(GENODE_DIR)/repos/base/recipes/src/base_content.inc

content: README
README:
	cp $(REP_DIR)/recipes/src/base-nova/README $@

content: src/kernel/nova
src/kernel:
	$(mirror_from_rep_dir)

KERNEL_PORT_DIR := $(call port_dir,$(REP_DIR)/ports/nova)

src/kernel/nova: src/kernel
	cp -r $(KERNEL_PORT_DIR)/src/kernel/nova/* $@

content:
	sed -i "s/nova_timer_drv/timer/" src/timer/nova/target.mk

