CONFIG_FILES = genode_ext2.vdi

content: $(CONFIG_FILES)

$(CONFIG_FILES):
	cp $(REP_DIR)/recipes/raw/genode_source_ext2/$@ $@
