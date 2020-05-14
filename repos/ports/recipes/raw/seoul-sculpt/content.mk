CONFIG_FILES = init.config vm_seoul.cfg vm_seoul_boot_1.cfg

content: $(CONFIG_FILES)

$(CONFIG_FILES):
	cp $(REP_DIR)/recipes/raw/seoul-sculpt/$@ $@
