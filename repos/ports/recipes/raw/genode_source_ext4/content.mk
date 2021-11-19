CONFIG_FILES = genode_ext4.vdi

content: $(CONFIG_FILES)

$(CONFIG_FILES):
	cp $(REP_DIR)/recipes/raw/genode_source_ext4/$@ $@
