include $(GENODE_DIR)/repos/base/recipes/src/base_content.inc

content: README
README:
	cp $(REP_DIR)/recipes/src/base-novae/README $@

content: src/kernel/novae

KERNEL_PORT_DIR := $(call port_dir,$(REP_DIR)/ports/novae)

src/kernel/novae:
	mkdir -p src/kernel/novae
	cp -r $(KERNEL_PORT_DIR)/src/kernel/novae/* $@

content:
	for spec in x86_64; do \
	  mv lib/mk/spec/$$spec/ld-novae.mk lib/mk/spec/$$spec/ld.mk; \
	  done;
	sed -i "s/novae_timer/timer/" src/timer/novae/target.mk

