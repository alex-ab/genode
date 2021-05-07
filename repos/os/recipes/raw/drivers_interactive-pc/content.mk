content: drivers.config fb_drv.config event_filter.config en_us.chargen \
         special.chargen drivers_platform.config

drivers.config fb_drv.config event_filter.config drivers_platform.config:
	cp $(REP_DIR)/recipes/raw/drivers_interactive-pc/$@ $@

en_us.chargen special.chargen:
	cp $(REP_DIR)/src/server/event_filter/$@ $@
