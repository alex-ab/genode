MIRROR_FROM_REP_DIR := lib/import/import-mesa-minimal.mk \
                       lib/import/import-mesa_api-minimal.mk \
                       lib/symbols/egl-minimal \
                       lib/symbols/mesa-minimal

content: $(MIRROR_FROM_REP_DIR)

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/mesa-minimal)

content: include

include:
	mkdir -p $@
	cp -r $(PORT_DIR)/include/* $@
	cp -r $(REP_DIR)/include/EGL $@

content: LICENSE FindOpenGL.cmake

LICENSE:
	cp $(PORT_DIR)/src/lib/mesa/docs/license.rst $@

Find%.cmake:
	echo 'set($*_FOUND True)' | sed -r 's/\w+_FOUND/\U&/' > $@
