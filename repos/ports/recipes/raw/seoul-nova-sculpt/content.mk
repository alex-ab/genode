CONFIG_FILES = init.config vm_seoul.cfg

content: $(CONFIG_FILES)

$(CONFIG_FILES):
	cp $(REP_DIR)/recipes/raw/seoul-nova-sculpt/$@ $@
