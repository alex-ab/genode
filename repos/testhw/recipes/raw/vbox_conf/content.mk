content: vbox_conf.tar

TAR = tar --owner=0 --group=0 --numeric-owner --mode='go=' --mtime='1970-01-01 00:00+00'

FILES = $(sort $(filter-out %~ hash content.mk,\
        $(notdir $(wildcard $(REP_DIR)/recipes/raw/vbox_conf/*))))

vbox_conf.tar:
	$(TAR) -cf $@ -C $(REP_DIR)/recipes/raw/vbox_conf $(FILES)
