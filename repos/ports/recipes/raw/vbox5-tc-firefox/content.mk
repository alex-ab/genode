CONFIG_FILES = machine.vbox tc_firefox_raw.vmdk usb_devices

content: $(CONFIG_FILES)

$(CONFIG_FILES):
	cp $(REP_DIR)/recipes/raw/vbox5-tc-firefox/$@ $@
