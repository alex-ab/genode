MIRROR_FROM_REP_DIR := \
	lib/mk/curl-minimal.inc \
	lib/mk/spec/32bit/curl-minimal.mk \
	lib/mk/spec/64bit/curl-minimal.mk

content: $(MIRROR_FROM_REP_DIR)

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/curl-minimal)

content: src/lib/curl

src/lib/curl:
	mkdir -p $@
	cp -r $(PORT_DIR)/src/lib/curl/lib $@/
	cp -r $(REP_DIR)/src/lib/curl/spec $@/

content: LICENSE

LICENSE:
	cp $(PORT_DIR)/src/lib/curl/COPYING $@
