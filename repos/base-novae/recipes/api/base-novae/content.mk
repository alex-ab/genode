FROM_BASE_NOVAE := etc include

# base-novae.lib.a depends on timeout.lib.a, which includes base/internal/gloabls.h
FROM_BASE := lib/mk/timeout.mk src/lib/timeout \
             src/include/base/internal/globals.h

content: $(FROM_BASE_NOVAE) $(FROM_BASE) LICENSE

$(FROM_BASE_NOVAE):
	$(mirror_from_rep_dir)

$(FROM_BASE):
	mkdir -p $(dir $@)
	cp -r $(GENODE_DIR)/repos/base/$@ $@

LICENSE:
	cp $(GENODE_DIR)/LICENSE $@
