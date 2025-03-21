include $(REP_DIR)/lib/mk/virtualbox7-common.inc

ifeq ($(shell which yasm),)
REQUIRES += installation_of_yasm
endif

SRC_O += VBoxPcBiosBinary8086.o  VBoxPcBiosBinary286.o  VBoxPcBiosBinary386.o
SRC_O += VBoxVgaBiosBinary8086.o VBoxVgaBiosBinary286.o VBoxVgaBiosBinary386.o
SRC_O += XVBoxBiosLogoBin.o
SRC_O += VBoxEFI-x86.o VBoxEFI-amd64.o

VBox%.o : VBox%.rom
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_ab$*, g_cb$*;" \
	               ".data;" \
	               "g_cb$*:; .long g_ab$*End - g_ab$*;" \
	               ".align 4096;" \
	               "g_ab$*:; .incbin \"$<\";" \
	               "g_ab$*End:;" | \
		$(AS) $(AS_OPT) -f -o $@ -

VBoxPcBiosBinary%.rom: Devices/PC/BIOS/VBoxBiosAlternative%.asm
	$(MSG_ASSEM)
	$(VERBOSE)yasm -w -f bin -o $@ $<

VBoxVgaBiosBinary%.rom: Devices/Graphics/BIOS/VBoxVgaBiosAlternative%.asm
	$(MSG_ASSEM)
	$(VERBOSE)yasm -w -f bin -o $@ $<

XVBoxBiosLogoBin.o: $(VBOX_DIR)/Devices/Graphics/BIOS/vbox_bios_splash.bmp
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abVgaDefBiosLogo, g_cbVgaDefBiosLogo;" \
	               ".data;" \
	               "g_cbVgaDefBiosLogo:; .long g_abVgaDefBiosLogoEnd - g_abVgaDefBiosLogo;" \
	               ".align 4096;" \
	               "g_abVgaDefBiosLogo:; .incbin \"$<\";" \
	               "g_abVgaDefBiosLogoEnd:;" | \
		$(AS) $(AS_OPT) -f -o $@ -

VBoxEFI-x86.o: $(VBOX_DIR)/Devices/EFI/FirmwareBin/VBoxEFI-x86.fd
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abEfiFirmwareX86, g_cbEfiFirmwareX86;" \
	               ".data;" \
	               ".align 4096;" \
	               "g_abEfiFirmwareX86:; .incbin \"$<\";" \
	               "g_cbEfiFirmwareX86:; .long g_cbEfiFirmwareX86 - g_abEfiFirmwareX86;" | \
		$(AS) $(AS_OPT) -f -o $@ -

VBoxEFI-amd64.o: $(VBOX_DIR)/Devices/EFI/FirmwareBin/VBoxEFI-amd64.fd
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abEfiFirmwareAmd64, g_cbEfiFirmwareAmd64;" \
	               ".data;" \
	               ".align 4096;" \
	               "g_abEfiFirmwareAmd64:; .incbin \"$<\";" \
	               "g_cbEfiFirmwareAmd64:; .long g_cbEfiFirmwareAmd64 - g_abEfiFirmwareAmd64;" | \
		$(AS) $(AS_OPT) -f -o $@ -

vpath $(VBOX_DIR)

CC_CXX_WARN_STRICT =
