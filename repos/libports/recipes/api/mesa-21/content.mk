MIRROR_FROM_REP_DIR := lib/import/import-mesa-21.mk \
                       lib/symbols/egl-21 \
                       lib/symbols/mesa-21

content: $(MIRROR_FROM_REP_DIR)

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/mesa-21)

content: include

include:
	mkdir -p $@
	cp -r $(PORT_DIR)/include/* $@
	cp -r $(REP_DIR)/include/EGL $@

content: LICENSE

LICENSE:
	cp $(PORT_DIR)/src/lib/mesa/docs/license.rst $@
