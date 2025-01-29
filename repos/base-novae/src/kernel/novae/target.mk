include $(REP_DIR)/etc/board.conf
TARGET = novae-$(BOARD)
LIBS   = kernel-novae-$(BOARD)

ifneq ($(filter x86_64, $(SPECS)),)

$(INSTALL_DIR)/$(TARGET):
	cp $(INSTALL_DIR)/../kernel/novae/x86_64-nova $@

else

$(INSTALL_DIR)/$(TARGET):
	cp $(INSTALL_DIR)/../kernel/novae/aarch64-$(BOARD)-nova $@

endif
