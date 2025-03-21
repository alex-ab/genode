/*
 * \brief  Dummy implementations of symbols needed by VirtualBox
 * \author Norman Feske
 * \author Christian Helmuth
 * \date   2013-08-22
 */

/*
 * Copyright (C) 2013-2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

/* Genode includes */
#include <base/sleep.h>
#include <util/string.h>

/* local includes */
#include "stub_macros.h"
#include "util.h"

/* vbox includes */
#include "PGMInternal.h"

static bool const debug = true;


/* ApplianceImplExport.cpp */

#include "MachineImpl.h"

HRESULT Machine::exportTo(const ComPtr<IAppliance> &aAppliance,
                          const com::Utf8Str &aLocation,
                          ComPtr<IVirtualSystemDescription> &aDescription) STOP


/* log.cpp */
RTR0DECL(int) RTLogSetR0ThreadNameV(PRTLOGGER, const char *, va_list) STOP


/* DisplayPNGUtil.cpp */

#include "DisplayImpl.h"

int DisplayMakePNG(uint8_t *, uint32_t, uint32_t, uint8_t **, uint32_t *,
                   uint32_t *, uint32_t *, uint8_t) STOP


/* initterm.cpp */

#include "VBox/com/com.h"

HRESULT com::Initialize(uint32_t) { return S_OK; }
HRESULT com::Shutdown()           STOP


/* USBFilter.cpp */

#include "VBox/usbfilter.h"

USBFILTERMATCH USBFilterGetMatchingMethod(PCUSBFILTER, USBFILTERIDX) STOP
char const *   USBFilterGetString        (PCUSBFILTER, USBFILTERIDX) STOP

int  USBFilterGetNum          (PCUSBFILTER, USBFILTERIDX) STOP
void USBFilterInit            (PUSBFILTER, USBFILTERTYPE) STOP
bool USBFilterIsMethodNumeric (USBFILTERMATCH)            STOP
bool USBFilterIsMethodString  (USBFILTERMATCH)            STOP
bool USBFilterIsNumericField  (USBFILTERIDX)              STOP
bool USBFilterIsStringField   (USBFILTERIDX)              STOP
bool USBFilterMatch           (PCUSBFILTER, PCUSBFILTER)  STOP
int  USBFilterSetIgnore       (PUSBFILTER, USBFILTERIDX)  STOP
int  USBFilterSetNumExact     (PUSBFILTER, USBFILTERIDX, uint16_t, bool)     STOP
int  USBFilterSetNumExpression(PUSBFILTER, USBFILTERIDX, const char *, bool) STOP
int  USBFilterSetStringExact  (PUSBFILTER, USBFILTERIDX, const char *, bool) STOP
int  USBFilterSetStringPattern(PUSBFILTER, USBFILTERIDX, const char *, bool) STOP
int  USBFilterSetStringExact  (PUSBFILTER, USBFILTERIDX, const char *, bool, bool) STOP
int  USBFilterMatchRated      (PCUSBFILTER, PCUSBFILTER) STOP


/* USBProxyBackend.cpp */

#include "USBProxyBackend.h"

USBProxyBackendFreeBSD::USBProxyBackendFreeBSD() STOP
USBProxyBackendFreeBSD::~USBProxyBackendFreeBSD() { }

int        USBProxyBackendFreeBSD::captureDevice(HostUSBDevice*) STOP
PUSBDEVICE USBProxyBackendFreeBSD::getDevices() STOP
int        USBProxyBackendFreeBSD::init(USBProxyService*, com::Utf8Str const&, com::Utf8Str const&, bool) STOP
int        USBProxyBackendFreeBSD::interruptWait() STOP
bool       USBProxyBackendFreeBSD::isFakeUpdateRequired() STOP
int        USBProxyBackendFreeBSD::releaseDevice(HostUSBDevice*) STOP
void       USBProxyBackendFreeBSD::uninit() STOP
int        USBProxyBackendFreeBSD::wait(unsigned int) STOP

USBProxyBackend::USBProxyBackend() STOP
USBProxyBackend::~USBProxyBackend() { }

HRESULT    USBProxyBackend::FinalConstruct() STOP
int        USBProxyBackend::captureDevice(HostUSBDevice*) STOP
void       USBProxyBackend::captureDeviceCompleted(HostUSBDevice*, bool) STOP
void       USBProxyBackend::deviceAdded(ComObjPtr<HostUSBDevice>&, USBDEVICE*) STOP
PUSBDEVICE USBProxyBackend::getDevices() STOP
HRESULT    USBProxyBackend::getName(com::Utf8Str&) STOP
HRESULT    USBProxyBackend::getType(com::Utf8Str&) STOP
bool       USBProxyBackend::i_isDevReEnumerationRequired() STOP
int        USBProxyBackend::init(USBProxyService*, com::Utf8Str const&, com::Utf8Str const&, bool) STOP
void *     USBProxyBackend::insertFilter(USBFILTER const*) STOP
int        USBProxyBackend::interruptWait() STOP
bool       USBProxyBackend::isFakeUpdateRequired() STOP
int        USBProxyBackend::releaseDevice(HostUSBDevice*) STOP
void       USBProxyBackend::releaseDeviceCompleted(HostUSBDevice*, bool) STOP
void       USBProxyBackend::removeFilter(void*) STOP
void       USBProxyBackend::serviceThreadInit() STOP
void       USBProxyBackend::serviceThreadTerm() STOP
void       USBProxyBackend::uninit() STOP
int        USBProxyBackend::wait(unsigned int) STOP

com::Utf8Str const &USBProxyBackend::i_getAddress()   STOP
com::Utf8Str const &USBProxyBackend::i_getBackend()   STOP
com::Utf8Str const &USBProxyBackend::i_getId()        STOP

USBProxyBackendUsbIp::USBProxyBackendUsbIp() STOP
USBProxyBackendUsbIp::~USBProxyBackendUsbIp() { }

int        USBProxyBackendUsbIp::captureDevice(HostUSBDevice*) STOP
PUSBDEVICE USBProxyBackendUsbIp::getDevices() STOP
int        USBProxyBackendUsbIp::init(USBProxyService*, com::Utf8Str const&, com::Utf8Str const&, bool) STOP
int        USBProxyBackendUsbIp::interruptWait() STOP
bool       USBProxyBackendUsbIp::isFakeUpdateRequired() STOP
int        USBProxyBackendUsbIp::releaseDevice(HostUSBDevice*) STOP
void       USBProxyBackendUsbIp::uninit() STOP
int        USBProxyBackendUsbIp::wait(unsigned int) STOP


/* AudioDriver.cpp */

#include "AudioDriver.h"

AudioDriver::AudioDriver(Console *) TRACE()
AudioDriver::~AudioDriver() { }
int AudioDriver::doDetachDriverViaEmt(PUVM, PCVMMR3VTABLE, util::AutoWriteLock *) STOP
int AudioDriver::doAttachDriverViaEmt(PUVM, PCVMMR3VTABLE, util::AutoWriteLock *) STOP
int AudioDriver::InitializeConfig(AudioDriverCfg *) { return VINF_SUCCESS; }


/* USBProxyService.cpp */

#include "USBProxyService.h"

USBProxyService::USBProxyService(Host* aHost) : mHost(aHost), mDevices(), mBackends() { }
USBProxyService::~USBProxyService() { }

HRESULT       USBProxyService::init() { return VINF_SUCCESS; }
RWLockHandle *USBProxyService::lockHandle() const                                  STOP
HRESULT       USBProxyService::autoCaptureDevicesForVM(SessionMachine *)           { return S_OK; }
HRESULT       USBProxyService::captureDeviceForVM(SessionMachine *, IN_GUID,
                                                  com::Utf8Str const&)             STOP
HRESULT       USBProxyService::detachAllDevicesFromVM(SessionMachine*, bool, bool) { return S_OK; }
HRESULT       USBProxyService::detachDeviceFromVM(SessionMachine*, IN_GUID, bool)  STOP
void         *USBProxyService::insertFilter(PCUSBFILTER aFilter)                   STOP
void          USBProxyService::removeFilter(void *aId)                             STOP
int           USBProxyService::getLastError()                                      { return VINF_SUCCESS; }
bool          USBProxyService::isActive()                                          { return false; }
HRESULT       USBProxyService::removeUSBDeviceSource(com::Utf8Str const&)          STOP
HRESULT       USBProxyService::addUSBDeviceSource(com::Utf8Str const&,
                                                  com::Utf8Str const&,
                                                  com::Utf8Str const&,
                                                  std::vector<com::Utf8Str, std::allocator<com::Utf8Str> > const&,
                                                  std::vector<com::Utf8Str, std::allocator<com::Utf8Str> > const&) STOP
HRESULT       USBProxyService::getDeviceCollection(std::vector<ComPtr<IHostUSBDevice>,
                                                   std::allocator<ComPtr<IHostUSBDevice> > >&) STOP

using USBDeviceSourceList =
	std::__cxx11::list<settings::USBDeviceSource, std::allocator<settings::USBDeviceSource> >;

HRESULT USBProxyService::i_saveSettings(USBDeviceSourceList &)       TRACE(VINF_SUCCESS)
HRESULT USBProxyService::i_loadSettings(USBDeviceSourceList const &) { return VINF_SUCCESS; }


/* USBFilter.cpp */

#include "VBox/usbfilter.h"

USBLIB_DECL(USBFILTERTYPE) USBFilterGetFilterType(PCUSBFILTER) STOP
USBLIB_DECL(int)           USBFilterSetFilterType(PUSBFILTER, USBFILTERTYPE) STOP


/* ApplianceImpl.cpp */

HRESULT VirtualBox::createAppliance(ComPtr<IAppliance> &) STOP


/* CloudProviderManagerImpl.cpp */

#include "CloudProviderManagerImpl.h"

CloudProviderManager::CloudProviderManager() : m_pVirtualBox(nullptr) { }
CloudProviderManager::~CloudProviderManager() { }

HRESULT CloudProviderManager::FinalConstruct()   { return VINF_SUCCESS; }
void    CloudProviderManager::FinalRelease()     TRACE()
HRESULT CloudProviderManager::init(VirtualBox *) { return VINF_SUCCESS; }
void    CloudProviderManager::uninit()           STOP
HRESULT CloudProviderManager::getProviderById       (com::Guid    const&, ComPtr<ICloudProvider>&) STOP
HRESULT CloudProviderManager::getProviderByName     (com::Utf8Str const&, ComPtr<ICloudProvider>&) STOP
HRESULT CloudProviderManager::getProviderByShortName(com::Utf8Str const&, ComPtr<ICloudProvider>&) STOP
HRESULT CloudProviderManager::getProviders(std::vector<ComPtr<ICloudProvider>,
                                           std::allocator<ComPtr<ICloudProvider> > >&) STOP


/* NetIf-freebsd.cpp */

#include "HostNetworkInterfaceImpl.h"
#include "netif.h"

int NetIfGetLinkSpeed(const char *, uint32_t *) STOP
int NetIfGetConfigByName(PNETIFINFO) STOP
int NetIfList(std::__cxx11::list<ComObjPtr<HostNetworkInterface>,
              std::allocator<ComObjPtr<HostNetworkInterface> > >&) { return VINF_SUCCESS; }


/* fatvfs.cpp */

#include "iprt/fsvfs.h"

RTDECL(int) RTFsFatVolFormat(RTVFSFILE, uint64_t, uint64_t, uint32_t, uint16_t,
                             uint16_t, RTFSFATTYPE, uint32_t, uint32_t,
                             uint8_t, uint16_t, uint32_t, PRTERRINFO) STOP

/* dvm.cpp */

#include "iprt/dvm.h"

RTDECL(uint32_t) RTDvmRelease(RTDVM)                                STOP
RTDECL(int)      RTDvmCreate(PRTDVM, RTVFSFILE, uint32_t, uint32_t) STOP
RTDECL(int)      RTDvmMapInitialize(RTDVM, const char *)            STOP


/* MachineImplMoveVM.cpp */

#include "MachineImplMoveVM.h"

HRESULT MachineMoveVM::init()                              STOP
void    MachineMoveVM::i_MoveVMThreadTask(MachineMoveVM *) STOP


/* HostDnsServiceResolvConf.cpp */

#include <string>
#include "HostDnsService.h"

HostDnsServiceResolvConf::~HostDnsServiceResolvConf() { }

HRESULT HostDnsServiceResolvConf::init(HostDnsMonitorProxy*, char const*) { return VINF_SUCCESS; }
void    HostDnsServiceResolvConf::uninit() STOP


/* HostVideoInputDeviceImpl.cpp */

#include "HostVideoInputDeviceImpl.h"

using VideoDeviceList =
	std::__cxx11::list<ComObjPtr<HostVideoInputDevice>,
	                   std::allocator<ComObjPtr<HostVideoInputDevice> > >;

HRESULT HostVideoInputDevice::queryHostDevices(VirtualBox*, VideoDeviceList *) STOP


/* HostUSBDeviceImpl.cpp */

#include "HostUSBDeviceImpl.h"

bool HostUSBDevice::i_isMatch(const USBDeviceFilter::BackupableUSBDeviceFilterData &) STOP


/* DhcpOptions.cpp */

#undef LOG_GROUP
#include "Dhcpd/DhcpOptions.h"

DhcpOption *DhcpOption::parse(unsigned char, int, char const*, int*) STOP


/* AutostartDb-generic.cpp */

#include "AutostartDb.h"

int AutostartDb::addAutostartVM   (char const *) STOP
int AutostartDb::addAutostopVM    (char const *) STOP
int AutostartDb::removeAutostopVM (char const *) STOP
int AutostartDb::removeAutostartVM(char const *) STOP

AutostartDb::AutostartDb()                       { }
AutostartDb::~AutostartDb()                      { }
int AutostartDb::setAutostartDbPath(char const*) { return VINF_SUCCESS; }

RT_C_DECLS_BEGIN

static_assert(sizeof(RTR0PTR) == sizeof(RTR3PTR), "pointer transformation bug");
static_assert(sizeof(RTR0PTR) == sizeof(void *) , "pointer transformation bug");
static_assert(sizeof(RTR3PTR) == sizeof(RTR0PTR), "pointer transformation bug");

int  emR3InitDbg(PVM)    { return VINF_SUCCESS; }
int  SELMR3Init(PVM)     { return VINF_SUCCESS; }
int  SELMR3Term(PVM)     { return VINF_SUCCESS; }
void SELMR3Relocate(PVM) { }
void SELMR3Reset(PVM)    { }

/* module loader of pluggable device manager */
int  pdmR3LdrInitU(PUVM)                              { return VINF_SUCCESS; }
int  PDMR3LdrLoadVMMR0U(PUVM)                         { return VINF_SUCCESS; }
void PDMR3LdrRelocateU(PUVM, RTGCINTPTR)              { }
int  pdmR3LoadR3U(PUVM, const char *, const char *)   { return VINF_SUCCESS; }
void pdmR3LdrTermU(PUVM)                              { }
int  PDMR3LdrLoadR0(PUVM, const char *, const char *) { return VINF_SUCCESS; }

char *pdmR3FileR3(const char * file, bool)
{
	char * pv = reinterpret_cast<char *>(RTMemTmpAllocZ(1));

	if (false)
		Genode::log(__func__, ": file ", file, " ", (void *)pv, " ", __builtin_return_address(0));

	return pv;
}

const char * RTBldCfgRevisionStr(void)
{
	return "Genode";
}

DECLHIDDEN(int) rtProcInitExePath(char *pszPath, size_t cchPath)
{
	Genode::copy_cstring(pszPath, "/virtualbox7", cchPath);

	return VINF_SUCCESS;
}

RT_C_DECLS_END


/* HostHardwareLinux.cpp */

#include "HostHardwareLinux.h"

int VBoxMainDriveInfo::updateDVDs() RT_NOEXCEPT { return VINF_SUCCESS; }


/* buildconfig.cpp */

#include <iprt/buildconfig.h>

uint32_t RTBldCfgRevision(void)     { return ~0; }
uint32_t RTBldCfgVersionBuild(void) { return ~0; }
uint32_t RTBldCfgVersionMajor(void) { return ~0; }
uint32_t RTBldCfgVersionMinor(void) { return ~0; }


/* VDIfTcpNet.cpp */

VBOXDDU_DECL(int) VDIfTcpNetInstDefaultCreate(PVDIFINST, PVDINTERFACE *) { return VINF_SUCCESS; }


/* ConsoleImplTeleporter.cpp */

#include <ConsoleImpl.h>

HRESULT Console::teleport(const com::Utf8Str &, ULONG, const com::Utf8Str &, ULONG, ComPtr<IProgress> &) STOP
HRESULT Console::i_teleporterTrg(PUVM, PCVMMR3VTABLE, IMachine *, Utf8Str *, bool, Progress *, bool *) STOP


/* DBGFBp.cpp */

#include <DBGFInternal.h>

int dbgfR3BpInit(VM*) { return VINF_SUCCESS; }


/* dbgcfg.cpp */

int RTDbgCfgCreate(PRTDBGCFG, const char *, bool)                          TRACE(VINF_SUCCESS)
int RTDbgCfgChangeUInt(RTDBGCFG, RTDBGCFGPROP, RTDBGCFGOP, uint64_t)       TRACE(VINF_SUCCESS)
int RTDbgCfgChangeString(RTDBGCFG, RTDBGCFGPROP, RTDBGCFGOP, const char *) TRACE(VINF_SUCCESS)


/* dbgas.cpp */

int RTDbgAsCreate(PRTDBGAS, RTUINTPTR, RTUINTPTR, const char *) TRACE(VINF_SUCCESS)

const char * RTDbgAsName(RTDBGAS hDbgAs) { return "RTDbgAsName dummy"; }

uint32_t RTDbgAsRetain(RTDBGAS)  { return 1; /* fake handle - UINT32_MAX is invalid */ }
uint32_t RTDbgAsRelease(RTDBGAS) { return 1; /* fake reference counter */ }


/* DBGFAddrSpace.cpp */

int  dbgfR3AsInit(PUVM) { return VINF_SUCCESS; }
void dbgfR3AsTerm(PUVM) { }
void dbgfR3AsRelocate(PUVM, RTGCUINTPTR) { }

int DBGFR3AsSymbolByAddr(PUVM, RTDBGAS, PCDBGFADDRESS, uint32_t,
                         PRTGCINTPTR, PRTDBGSYMBOL, PRTDBGMOD) TRACE(VERR_NOT_IMPLEMENTED)

PRTDBGSYMBOL DBGFR3AsSymbolByAddrA(PUVM, RTDBGAS, PCDBGFADDRESS, uint32_t,
                                   PRTGCINTPTR, PRTDBGMOD)
{
	return nullptr;
}

PRTDBGLINE DBGFR3AsLineByAddrA(PUVM, RTDBGAS, PCDBGFADDRESS,
                               PRTGCINTPTR, PRTDBGMOD)
{
	return nullptr;
}


int DBGFR3AsSymbolByName(PUVM, RTDBGAS, const char *, PRTDBGSYMBOL, PRTDBGMOD) STOP
int DBGFR3AsSetAlias(PUVM, RTDBGAS, RTDBGAS) STOP
RTDBGAS  DBGFR3AsResolveAndRetain(PUVM, RTDBGAS) STOP
RTDBGCFG DBGFR3AsGetConfig(PUVM) STOP


/* DBGFR3ModInMem.cpp */

int DBGFR3ModInMem(PUVM, PCDBGFADDRESS, uint32_t, const char *, const char *,
                   RTLDRARCH, uint32_t, PRTDBGMOD, PRTERRINFO) STOP


/* PGMMap.cpp */

#include <VBox/vmm/pgm.h>

VMMR3DECL(int) PGMR3MappingsSize(PVM pVM, uint32_t *pcb)
{
	*pcb = 0;
	return VINF_SUCCESS;
}


/* PGMSavedState.cpp */
int  pgmR3InitSavedState(PVM, ::uint64_t) { return VINF_SUCCESS; }


/* nsProxyRelease.cpp */

#include "nsProxyRelease.h"

NS_COM nsresult NS_ProxyRelease(nsIEventTarget *target, nsISupports *doomed, PRBool alwaysProxy) STOP


/* SUPR3HardenedVerify.cpp */

#include "SUPLibInternal.h"

DECLHIDDEN(int) supR3HardenedRecvPreInitData(PCSUPPREINITDATA) STOP


/* VBoxXPCOMImpImp.c */

void *_ZTV14nsGetInterface = nullptr;


/* VBox/Runtime/generic/http-curl.cpp (cURL based http-client) */
#include <iprt/http.h>

RTR3DECL(int) RTHttpCreate(PRTHTTP) STOP
RTR3DECL(int) RTHttpDestroy(RTHTTP) STOP
RTR3DECL(int) RTHttpQueryProxyInfoForUrl(RTHTTP, const char *, PRTHTTPPROXYINFO) STOP
RTR3DECL(int) RTHttpFreeProxyInfo(PRTHTTPPROXYINFO) STOP
RTR3DECL(int) RTHttpUseSystemProxySettings(RTHTTP) STOP


/* GuestFsInfoImpl.cpp */
#include <GuestFsInfoImpl.h>

GuestFsInfo::GuestFsInfo() STOP
GuestFsInfo::~GuestFsInfo() STOP

int  GuestFsInfo::init(PCGSTCTLFSINFO) STOP
void GuestFsInfo::uninit(void) STOP

HRESULT GuestFsInfo::FinalConstruct(void) STOP
void GuestFsInfo::FinalRelease() TRACE()

HRESULT GuestFsInfo::getFreeSize(LONG64 *) STOP
HRESULT GuestFsInfo::getTotalSize(LONG64 *) STOP
HRESULT GuestFsInfo::getBlockSize(ULONG *) STOP
HRESULT GuestFsInfo::getSectorSize(ULONG *) STOP
HRESULT GuestFsInfo::getSerialNumber(ULONG *) STOP
HRESULT GuestFsInfo::getIsRemote(BOOL *) STOP
HRESULT GuestFsInfo::getIsCaseSensitive(BOOL *) STOP
HRESULT GuestFsInfo::getIsReadOnly(BOOL *) STOP
HRESULT GuestFsInfo::getIsCompressed(BOOL *) STOP
HRESULT GuestFsInfo::getSupportsFileCompression(BOOL *) STOP
HRESULT GuestFsInfo::getMaxComponent(ULONG *) STOP
HRESULT GuestFsInfo::getType(com::Utf8Str &) STOP
HRESULT GuestFsInfo::getLabel(com::Utf8Str &) STOP
HRESULT GuestFsInfo::getMountPoint(com::Utf8Str &) STOP


/* UsbCardReader.cpp */
#include "UsbCardReader.h"

UsbCardReader::UsbCardReader(Console *console) : mParent(console) { }
UsbCardReader::~UsbCardReader() { }
int UsbCardReader::VRDENotify(uint32_t, void *, uint32_t) STOP
int UsbCardReader::VRDEResponse(int, void *, uint32_t, void *, uint32_t) STOP


/* HostDriveImpl.cpp */
#include "HostDriveImpl.h"

HostDrive::HostDrive() STOP
HostDrive::~HostDrive() STOP

HRESULT HostDrive::FinalConstruct() STOP
void HostDrive::FinalRelease() TRACE()

void HostDrive::uninit() STOP

HRESULT HostDrive::getDrivePath(com::Utf8Str &) STOP
HRESULT HostDrive::getPartitioningType(PartitioningType_T *) STOP
HRESULT HostDrive::getUuid(com::Guid &) STOP
HRESULT HostDrive::getSectorSize(ULONG *) STOP
HRESULT HostDrive::getSize(LONG64 *) STOP
HRESULT HostDrive::getModel(com::Utf8Str &) STOP
HRESULT HostDrive::getPartitions(std::vector<ComPtr<IHostDrivePartition> > &) STOP


/* HostHardwareLinux.cpp */
#include "HostHardwareLinux.h"

int VBoxMainDriveInfo::updateFixedDrives() RT_NOEXCEPT STOP


/* HostDriveImpl.cpp */
#include "HostDriveImpl.h"

HRESULT HostDrive::initFromPathAndModel(const com::Utf8Str &, const com::Utf8Str &) STOP

#include <iprt/crypto/x509.h>

/* x509-create-sign.cpp */
RTDECL(int) RTCrX509Certificate_GenerateSelfSignedRsa(RTDIGESTTYPE, uint32_t, uint32_t,
                                                      uint32_t, uint64_t, const char *,
                                                      const char *, const char *, PRTERRINFO) STOP

/* x509-file.cpp */
RTDECL(int) RTCrX509Certificate_ReadFromFile(PRTCRX509CERTIFICATE, const char *, uint32_t,
                                             PCRTASN1ALLOCATORVTABLE, PRTERRINFO) STOP


/* x509-core.cpp */
RTDECL(bool) RTCrX509Validity_IsValidAtTimeSpec(PCRTCRX509VALIDITY, PCRTTIMESPEC)  STOP


/* VBoxRTImpImp.c (generated) */
void RTCrX509Certificate_Delete(PRTCRX509CERTIFICATE) STOP


#include <iprt/sha.h>

/* openssl-sha256.cpp */
RTDECL(void) RTSha256(const void *, size_t, uint8_t pabHash[RTSHA256_HASH_SIZE]) STOP


/* sha256str.cpp */
RTDECL(int) RTSha256ToString(uint8_t const pabDigest[RTSHA256_HASH_SIZE], char *, size_t) STOP


/* openssl-sha1.cpp/alt-sha1.cpp */
RTDECL(void) RTSha1(const void *, size_t, uint8_t pabDigest[RTSHA1_HASH_SIZE]) STOP


/* sha1str.cpp */
RTDECL(int) RTSha1ToString(uint8_t const pabDigest[RTSHA1_HASH_SIZE], char *, size_t) STOP


/* devm.cpp */
#include "internal/dvm.h"

RTDECL(int) RTDvmMapOpen(RTDVM) STOP
RTDECL(int) RTDvmMapQueryTableLocations(RTDVM, uint32_t,
                                        PRTDVMTABLELOCATION, size_t, size_t *) STOP
RTDECL(RTDVMFORMATTYPE) RTDvmMapGetFormatType(RTDVM) STOP
RTDECL(uint32_t) RTDvmMapGetValidVolumes(RTDVM) STOP
RTDECL(int) RTDvmMapQueryFirstVolume(RTDVM, PRTDVMVOLUME) STOP
RTDECL(int) RTDvmMapQueryNextVolume(RTDVM, RTDVMVOLUME, PRTDVMVOLUME) STOP
RTDECL(uint32_t) RTDvmVolumeRelease(RTDVMVOLUME) STOP
RTDECL(uint64_t) RTDvmVolumeGetSize(RTDVMVOLUME) STOP
RTDECL(int) RTDvmVolumeQueryRange(RTDVMVOLUME, uint64_t *, uint64_t *) STOP
RTDECL(uint32_t) RTDvmVolumeGetIndex(RTDVMVOLUME, RTDVMVOLIDX) STOP


/* DBGFAllBp.cpp */
#include <VBox/vmm/dbgf.h>

VMM_INT_DECL(VBOXSTRICTRC) DBGFTrap03Handler(PVMCC, PVMCPUCC, PCPUMCTX) STOP


/* PDMAllNetShaper.cpp */
#include <VBox/vmm/pdmnetshaper.h>

VMM_INT_DECL(bool) PDMNetShaperAllocateBandwidth(PVMCC, PPDMNSFILTER, size_t) STOP
