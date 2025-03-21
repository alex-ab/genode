include $(REP_DIR)/lib/mk/virtualbox7-common.inc

LIBS  += stdcxx

SRC_CC += Main/xml/Settings.cpp

SRC_CC += Main/src-all/AudioUtils.cpp
SRC_CC += Main/src-all/AuthLibrary.cpp
SRC_CC += Main/src-all/AutoCaller.cpp
SRC_CC += Main/src-all/ConsoleSharedFolderImpl.cpp
SRC_CC += Main/src-all/CryptoUtils.cpp
SRC_CC += Main/src-all/EventImpl.cpp
SRC_CC += Main/src-all/DisplayResampleImage.cpp
SRC_CC += Main/src-all/DisplayUtils.cpp
SRC_CC += Main/src-all/Global.cpp
SRC_CC += Main/src-all/GlobalStatusConversion.cpp
SRC_CC += Main/src-all/HashedPw.cpp
SRC_CC += Main/src-all/NvramStoreImpl.cpp
SRC_CC += Main/src-all/PCIDeviceAttachmentImpl.cpp
SRC_CC += Main/src-all/ProgressImpl.cpp
SRC_CC += Main/src-all/RecordingUtils.cpp
SRC_CC += Main/src-all/SecretKeyStore.cpp
SRC_CC += Main/src-all/SharedFolderImpl.cpp
SRC_CC += Main/src-all/ThreadTask.cpp
SRC_CC += Main/src-all/VirtualBoxBase.cpp
SRC_CC += Main/src-all/VirtualBoxErrorInfoImpl.cpp
SRC_CC += Main/src-server/AudioAdapterImpl.cpp
SRC_CC += Main/src-server/AudioSettingsImpl.cpp
SRC_CC += Main/src-server/BandwidthControlImpl.cpp
SRC_CC += Main/src-server/BandwidthGroupImpl.cpp
SRC_CC += Main/src-server/CPUProfileImpl.cpp
SRC_CC += Main/src-server/ClientToken.cpp
SRC_CC += Main/src-server/ClientWatcher.cpp
SRC_CC += Main/src-server/DHCPConfigImpl.cpp
SRC_CC += Main/src-server/DHCPServerImpl.cpp
SRC_CC += Main/src-server/DataStreamImpl.cpp
SRC_CC += Main/src-server/FirmwareSettingsImpl.cpp
SRC_CC += Main/src-server/GraphicsAdapterImpl.cpp
SRC_CC += Main/src-server/GuestDebugControlImpl.cpp
SRC_CC += Main/src-server/GuestOSTypeImpl.cpp
SRC_CC += Main/src-server/HostDnsService.cpp
SRC_CC += Main/src-server/HostImpl.cpp
SRC_CC += Main/src-server/HostNetworkInterfaceImpl.cpp
SRC_CC += Main/src-server/HostPower.cpp
SRC_CC += Main/src-server/HostX86Impl.cpp
SRC_CC += Main/src-server/MachineImpl.cpp
SRC_CC += Main/src-server/MachineImplCloneVM.cpp
SRC_CC += Main/src-server/Matching.cpp
SRC_CC += Main/src-server/MediumAttachmentImpl.cpp
SRC_CC += Main/src-server/MediumFormatImpl.cpp
SRC_CC += Main/src-server/MediumIOImpl.cpp
SRC_CC += Main/src-server/MediumImpl.cpp
SRC_CC += Main/src-server/MediumLock.cpp
SRC_CC += Main/src-server/NATEngineImpl.cpp 
SRC_CC += Main/src-server/NATNetworkImpl.cpp
SRC_CC += Main/src-server/NetworkAdapterImpl.cpp
SRC_CC += Main/src-server/NetworkServiceRunner.cpp
SRC_CC += Main/src-server/ParallelPortImpl.cpp
SRC_CC += Main/src-server/Performance.cpp
SRC_CC += Main/src-server/PerformanceImpl.cpp
SRC_CC += Main/src-server/PlatformImpl.cpp
SRC_CC += Main/src-server/PlatformPropertiesImpl.cpp
SRC_CC += Main/src-server/PlatformX86Impl.cpp
SRC_CC += Main/src-server/ProgressProxyImpl.cpp
SRC_CC += Main/src-server/RecordingScreenSettingsImpl.cpp
SRC_CC += Main/src-server/RecordingSettingsImpl.cpp
SRC_CC += Main/src-server/SerialPortImpl.cpp
SRC_CC += Main/src-server/SnapshotImpl.cpp
SRC_CC += Main/src-server/StorageControllerImpl.cpp
SRC_CC += Main/src-server/SystemPropertiesImpl.cpp
SRC_CC += Main/src-server/TokenImpl.cpp
SRC_CC += Main/src-server/TrustedPlatformModuleImpl.cpp
SRC_CC += Main/src-server/USBControllerImpl.cpp
SRC_CC += Main/src-server/USBDeviceFilterImpl.cpp
SRC_CC += Main/src-server/USBDeviceFiltersImpl.cpp
SRC_CC += Main/src-server/UefiVariableStoreImpl.cpp
SRC_CC += Main/src-server/VRDEServerImpl.cpp
SRC_CC += Main/src-server/VirtualBoxImpl.cpp
SRC_CC += Main/src-server/generic/NetIf-generic.cpp

# use OS/2 version of 'pm::createHAL()' because it is empty
SRC_CC += Main/src-server/os2/PerformanceOs2.cpp

# generated from VBox/Main/idl/comimpl.xsl
SRC_CC += Main/VBoxEvents.cpp

# generated from VBox/Main/idl/stringify-enums.xsl
SRC_CC += Main/StringifyEnums.cpp

# see comment in virtualbox7-client.mk
CC_OPT_Main/src-server/MediumImpl = -Wno-enum-compare

# prevent double define of 'LOG_GROUP'
VBOX_CC_OPT += -DIN_VBOXSVC

# generate certificates
SRC_O += cert_MicCorKEKCA2011_2011-06-24.o \
         cert_MicCorUEFCA2011_2011-06-27.o \
         cert_microsoft_corporation_kek_2k_ca_2023.o \
         cert_microsoft_option_rom_uefi_ca_2023.o \
         cert_microsoft_uefi_ca_2023.o \
         cert_MicWinProPCA2011_2011-10-19.o \
         cert_OrclUefiDefPk2021_2021-09-29.o \
         cert_windows_uefi_ca_2023.o

cert_MicCorKEKCA2011_2011-06-24.o: MicCorKEKCA2011_2011-06-24.crt
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abUefiMicrosoftKek, g_cbUefiMicrosoftKek;" \
	               ".data;" \
	               "g_abUefiMicrosoftKek:; .incbin \"$<\";" \
	               "g_cbUefiMicrosoftKek:; .long g_cbUefiMicrosoftKek - g_abUefiMicrosoftKek; " | \
		$(AS) $(AS_OPT) -f -o $@ -

cert_MicCorUEFCA2011_2011-06-27.o: MicCorUEFCA2011_2011-06-27.crt
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abUefiMicrosoft3rdCa, g_cbUefiMicrosoft3rdCa;" \
	               ".data;" \
	               "g_abUefiMicrosoft3rdCa:; .incbin \"$<\";" \
	               "g_cbUefiMicrosoft3rdCa:; .long g_cbUefiMicrosoft3rdCa - g_abUefiMicrosoft3rdCa; " | \
		$(AS) $(AS_OPT) -f -o $@ -

cert_microsoft_corporation_kek_2k_ca_2023.o: microsoft_corporation_kek_2k_ca_2023.crt
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abUefiMicrosoftKek2023, g_cbUefiMicrosoftKek2023;" \
	               ".data;" \
	               "g_abUefiMicrosoftKek2023:; .incbin \"$<\";" \
	               "g_cbUefiMicrosoftKek2023:; .long g_cbUefiMicrosoftKek2023 - g_abUefiMicrosoftKek2023; " | \
		$(AS) $(AS_OPT) -f -o $@ -

cert_microsoft_option_rom_uefi_ca_2023.o: microsoft_option_rom_uefi_ca_2023.crt
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abUefiMicrosoftOpRomUefiCa2023, g_cbUefiMicrosoftOpRomUefiCa2023;" \
	               ".data;" \
	               "g_abUefiMicrosoftOpRomUefiCa2023:; .incbin \"$<\";" \
	               "g_cbUefiMicrosoftOpRomUefiCa2023:; .long g_cbUefiMicrosoftOpRomUefiCa2023 - g_abUefiMicrosoftOpRomUefiCa2023; " | \
		$(AS) $(AS_OPT) -f -o $@ -

cert_microsoft_uefi_ca_2023.o: microsoft_uefi_ca_2023.crt
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abUefiMicrosoft3rdCa2023, g_cbUefiMicrosoft3rdCa2023;" \
	               ".data;" \
	               "g_abUefiMicrosoft3rdCa2023:; .incbin \"$<\";" \
	               "g_cbUefiMicrosoft3rdCa2023:; .long g_cbUefiMicrosoft3rdCa2023 - g_abUefiMicrosoft3rdCa2023;" | \
		$(AS) $(AS_OPT) -f -o $@ -

cert_MicWinProPCA2011_2011-10-19.o: MicWinProPCA2011_2011-10-19.crt
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abUefiMicrosoftWinCa, g_cbUefiMicrosoftWinCa;" \
	               ".data;" \
	               "g_abUefiMicrosoftWinCa:; .incbin \"$<\";" \
	               "g_cbUefiMicrosoftWinCa:; .long g_cbUefiMicrosoftWinCa - g_abUefiMicrosoftWinCa;" | \
		$(AS) $(AS_OPT) -f -o $@ -

cert_OrclUefiDefPk2021_2021-09-29.o: OrclUefiDefPk2021_2021-09-29.crt
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abUefiOracleDefPk, g_cbUefiOracleDefPk;" \
	               ".data;" \
	               "g_abUefiOracleDefPk:; .incbin \"$<\";" \
	               "g_cbUefiOracleDefPk:; .long g_cbUefiOracleDefPk - g_abUefiOracleDefPk;" | \
		$(AS) $(AS_OPT) -f -o $@ -

cert_windows_uefi_ca_2023.o: windows_uefi_ca_2023.crt
	$(MSG_CONVERT)$@
	$(VERBOSE)echo ".global g_abUefiMicrosoftWinCa2023, g_cbUefiMicrosoftWinCa2023;" \
	               ".data;" \
	               "g_abUefiMicrosoftWinCa2023:; .incbin \"$<\";" \
	               "g_cbUefiMicrosoftWinCa2023:; .long g_cbUefiMicrosoftWinCa2023 - g_abUefiMicrosoftWinCa2023;" | \
		$(AS) $(AS_OPT) -f -o $@ -

vpath %.crt $(VBOX_DIR)/Main/Certificates

INC_DIR += $(VBOX_DIR)/Main/xml
INC_DIR += $(VBOX_DIR)/Main/include
INC_DIR += $(VIRTUALBOX_DIR)/VBoxAPIWrap
INC_DIR += $(VIRTUALBOX_DIR)/include/VBox/Graphics

CC_CXX_WARN_STRICT =
