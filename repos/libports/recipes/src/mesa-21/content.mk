MIRROR_FROM_REP_DIR := lib/mk/egl-21.mk \
                       lib/mk/egl_swrast-21.mk \
                       lib/mk/glapi-21.mk \
                       lib/mk/mesa-21.mk \
                       lib/mk/mesa-21_api.mk \
                       lib/mk/mesa-common-21.inc \
                       lib/mk/softpipe.mk \
                       src/lib/mesa-21

content: $(MIRROR_FROM_REP_DIR) src/lib/mesa-21/target.mk

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

src/lib/mesa-21/target.mk:
	mkdir -p $(dir $@)
	echo "LIBS = mesa-21" > $@

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/mesa-21)

MIRROR_FROM_PORT_DIR := src/lib/mesa/src generated \

content: $(MIRROR_FROM_PORT_DIR)

$(MIRROR_FROM_PORT_DIR):
	mkdir -p $(dir $@)
	cp -r $(PORT_DIR)/$@ $(dir $@)

content: LICENSE

LICENSE:
	cp $(PORT_DIR)/src/lib/mesa/docs/license.rst $@
