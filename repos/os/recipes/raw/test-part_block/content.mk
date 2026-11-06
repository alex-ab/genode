RAW_IMAGES := ata.gpt.raw ata.mbr.raw ata.ahdi.raw
RAW_CONFIG := disk.config mbr.config gpt.config ahdi.config
RAW_DIR    := $(REP_DIR)/recipes/raw/test-part_block

content: $(RAW_IMAGES) $(RAW_CONFIG)

$(RAW_IMAGES):
	tar -xzvf $(RAW_DIR)/$@.tar.gz 1>/dev/null

$(RAW_CONFIG):
	cp $(RAW_DIR)/$@ $@


