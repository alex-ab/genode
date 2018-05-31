CONFIG_FILES = tc-firefox.raw

content: $(CONFIG_FILES)

$(CONFIG_FILES):
	cp $(REP_DIR)/recipes/raw/vm-tc-firefox/$@ $@
