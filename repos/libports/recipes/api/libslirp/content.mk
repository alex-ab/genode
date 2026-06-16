MIRROR_FROM_REP_DIR := lib/mk/libslirp.inc \
                       lib/mk/spec/x86_32/libslirp.mk \
                       lib/mk/spec/x86_64/libslirp.mk \
                       lib/import/import-libslirp.mk \
                       src/lib/libslirp/spec/x86_32/check_glib.c

content: $(MIRROR_FROM_REP_DIR)

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/libslirp)
MIRROR_FROM_PORT_DIR := include src/lib/libslirp/src

content: $(addprefix include/slirp/,libslirp.h libslirp-version.h) $(MIRROR_FROM_PORT_DIR)

$(MIRROR_FROM_PORT_DIR):
	mkdir -p $(dir $@)
	cp -r $(PORT_DIR)/$@ $@

include/slirp/libslirp.h:
	mkdir -p include/slirp
	cp $(PORT_DIR)/include/slirp/libslirp.h include/slirp/libslirp.h

include/slirp/libslirp-version.h:
	mkdir -p include/slirp
	cp $(REP_DIR)/src/lib/libslirp/libslirp-version.h include/slirp/libslirp-version.h

content: LICENSE

LICENSE:
	cp $(PORT_DIR)/src/lib/libslirp/COPYRIGHT $@

