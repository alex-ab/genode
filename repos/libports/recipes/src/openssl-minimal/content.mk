MIRROR_FROM_REP_DIR := \
	lib/mk/libcrypto-minimal.inc \
	lib/mk/spec/arm/libcrypto-minimal.mk \
	lib/mk/spec/arm_64/libcrypto-minimal.mk \
	lib/mk/spec/x86_32/libcrypto-minimal.mk \
	lib/mk/spec/x86_64/libcrypto-minimal.mk \
	lib/mk/libssl-minimal.mk

content: $(MIRROR_FROM_REP_DIR)

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/openssl-minimal)

content: src/lib/openssl

src/lib/openssl:
	mkdir -p $@
	cp -r $(PORT_DIR)/src/lib/openssl/crypto $@/
	cp    $(PORT_DIR)/src/lib/openssl/e_os.h $@/
	cp -r $(PORT_DIR)/src/lib/openssl/include $@/
	cp -r $(PORT_DIR)/src/lib/openssl/ssl $@/
	cp -r $(REP_DIR)/src/lib/openssl/crypto $@/
	cp -r $(REP_DIR)/src/lib/openssl/spec $@/

content: LICENSE

LICENSE:
	cp $(PORT_DIR)/src/lib/openssl/LICENSE $@
