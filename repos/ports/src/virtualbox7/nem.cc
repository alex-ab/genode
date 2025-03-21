/*
 * \brief  Genode backend for VirtualBox native execution manager
 * \author Norman Feske
 * \author Christian Helmuth
 * \date   2020-11-05
 */

/*
 * Copyright (C) 2020-2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

/* VirtualBox includes */
#include <VBox/vmm/cpum.h>      /* must be included before CPUMInternal.h */
#include <VBox/vmm/tm.h>        /* must be included before TMInternal.h */
#define VMCPU_INCL_CPUM_GST_CTX /* needed for cpum.GstCtx */
#include <CPUMInternal.h>       /* enable access to cpum.s.* */
#include <HMInternal.h>         /* enable access to hm.s.* */
#include <TMInternal.h>         /* enable access to tm.s.* */
#define RT_OS_WINDOWS           /* needed for definition all nem.s members */
#include <NEMInternal.h>        /* enable access to nem.s.* */
#undef RT_OS_WINDOWS
#include <PGMInternal.h>        /* enable access to pgm.s.* */
#include <VBox/vmm/vmcc.h>      /* must be included before PGMInline.h */
#include <PGMInline.h>
#include <VBox/vmm/nem.h>
#include <VBox/apic.h>
#include <VBox/vmm/em.h>
#include <VBox/err.h>

/* Genode includes */
#include <base/mutex.h>

/* local includes */
#include <stub_macros.h>
#include <sup.h>
#include <sup_vcpu.h>
#include <sup_gmm.h>
#include <sup_vm.h>

static bool const debug = true;

using namespace Genode;


namespace Sup { struct Nem; }

struct Sup::Nem
{
	Gmm &_gmm;

	using Protection = Sup::Gmm::Protection;

	struct Range
	{
		addr_t first_byte { 0 };
		addr_t last_byte  { 0 };

		Protection prot { false, false, false };

		size_t size() const { return last_byte ? last_byte - first_byte + 1 : 0; }

		/* empty ranges are invalid */
		bool valid() const { return size() != 0; }

		enum class Extend_result { PREPENDED, APPENDED, FAILED };

		Extend_result extend(Range const &other)
		{
			/* ignore invalid ranges */
			if (!other.valid())
				return Extend_result::APPENDED;

			if (!(prot == other.prot))
				return Extend_result::FAILED;

			/* initialize if uninitialized */
			if (!valid()) {
				first_byte = other.first_byte;
				last_byte  = other.last_byte;
				prot       = other.prot;

				return Extend_result::APPENDED;
			}

			/* prepend */
			if (first_byte == other.last_byte + 1) {
				first_byte = other.first_byte;

				return Extend_result::PREPENDED;
			}

			/* append */
			if (last_byte + 1 == other.first_byte) {
				last_byte = other.last_byte;

				return Extend_result::APPENDED;
			}

			/* not contiguous (which includes overlaps) */
			return Extend_result::FAILED;
		}

		void print(Output &o) const
		{
			Genode::print(o, prot, ":", Hex_range(first_byte, size()));
		}
	};

	Mutex mutex       { };
	Range host_range  { };
	Range guest_range { };

	void commit_range_unsynchronized()
	{
		/* ignore commit of invalid ranges */
		if (!host_range.valid())
			return;

		/* commit the current range to GMM */
		_gmm.map_to_guest(Gmm::Vmm_addr   { host_range.first_byte },
		                  Gmm::Guest_addr { guest_range.first_byte },
		                  Gmm::Pages      { host_range.size() >> PAGE_SHIFT },
		                  host_range.prot);

		/* reset ranges */
		host_range  = { };
		guest_range = { };
	}

	void commit_range()
	{
		Mutex::Guard guard(mutex);

		commit_range_unsynchronized();
	}

	void map_to_guest(addr_t host_addr, addr_t guest_addr, size_t size, Protection prot)
	{
		Mutex::Guard guard(mutex);

		Range new_host_range  { host_addr,  host_addr  + (size - 1), prot };
		Range new_guest_range { guest_addr, guest_addr + (size - 1), prot };

		/* new page just extends the current ranges */
		Range::Extend_result const host_extend_result  = new_host_range.extend(host_range);
		Range::Extend_result const guest_extend_result = new_guest_range.extend(guest_range);

		bool const failed      = (host_extend_result == Range::Extend_result::FAILED);
		bool const same_result = (host_extend_result == guest_extend_result);

		if (!failed && same_result) {
			host_range  = new_host_range;
			guest_range = new_guest_range;

			return;
		}

		/* new page starts a new range */
		commit_range_unsynchronized();

		/* start over with new page */
		host_range  = { host_addr,  host_addr  + (size - 1), prot };
		guest_range = { guest_addr, guest_addr + (size - 1), prot };
	}

	void map_page_to_guest(addr_t host_addr, addr_t guest_addr, Protection prot)
	{
		map_to_guest(host_addr, guest_addr, X86_PAGE_SIZE, prot);
	}

	Gmm & gmm() { return _gmm; }

	Nem(Gmm &gmm) : _gmm(gmm) { }
};


Sup::Nem * nem_ptr;

void Sup::nem_init(Gmm &gmm)
{
	nem_ptr = new Nem(gmm);
}


VMM_INT_DECL(int) NEMImportStateOnDemand(PVMCPUCC pVCpu, ::uint64_t fWhat) STOP


VMM_INT_DECL(int) NEMHCQueryCpuTick(PVMCPUCC pVCpu, ::uint64_t *pcTicks,
                                    ::uint32_t *puAux) STOP


VMM_INT_DECL(int) NEMHCResumeCpuTickOnAll(PVMCC pVM, PVMCPUCC pVCpu,
                                          ::uint64_t uPausedTscValue) STOP



DECLHIDDEN(int) nemR3NativeInitCompletedRing3(PVM pVM)
{
	return VINF_SUCCESS;
}


void nemHCNativeNotifyHandlerPhysicalRegister(PVMCC pVM,
                                              PGMPHYSHANDLERKIND enmKind,
                                              RTGCPHYS GCPhys, RTGCPHYS cb)
{
}


int nemR3NativeInit(PVM pVM, bool fFallback, bool fForced)
{
	VM_SET_MAIN_EXECUTION_ENGINE(pVM, VM_EXEC_ENGINE_NATIVE_API);

	/*
	 * Explicitly enable NEM mode - forced by MMIO2 assertions and PGM_PAGE_INIT
	 * with host address 0x0000ffffffff0000  (see: pgmPhysMmio2RegisterWorker)
	 */
	PGMR3EnableNemMode(pVM);

	return VINF_SUCCESS;
}


int nemR3NativeInitAfterCPUM(PVM pVM)
{
	return VINF_SUCCESS;
}


int nemR3NativeInitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
	return VINF_SUCCESS;
}


int nemR3NativeTerm(PVM pVM)
{
	return VINF_SUCCESS;
}


/**
 * VM reset notification.
 *
 * @param   pVM         The cross context VM structure.
 */
void nemR3NativeReset(PVM pVM) TRACE()


/**
 * Reset CPU due to INIT IPI or hot (un)plugging.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the CPU being
 *                      reset.
 * @param   fInitIpi    Whether this is the INIT IPI or hot (un)plugging case.
 */
void nemR3NativeResetCpu(PVMCPU pVCpu, bool fInitIpi) { }


VMMR3_INT_DECL(VBOXSTRICTRC) NEMR3RunGC(PVM pVM, PVMCPU pVCpu)
{
	using namespace Sup;

	/*
	 * Program the watchdog timer near the next expiring virtual sync timeout.
	 * Without this code the watchdog timer would be programmed to a fixed
	 * interval (10ms by default), which could be too high or cause too much
	 * CPU load if set lower. Other hosts use the VMX preemption timer, which
	 * is currently not available on Genode.
	 */
	{
		static ::uint64_t current_interval_ns { 0 };
		static Mutex interval_mutex { };

		Mutex::Guard guard(interval_mutex);

		::uint64_t new_interval_ns = TMVirtualSyncGetNsToDeadline(pVM, nullptr, nullptr);
		new_interval_ns = (new_interval_ns / RT_NS_1MS) * RT_NS_1MS;
		new_interval_ns = max(new_interval_ns, 1 * RT_NS_1MS);
		new_interval_ns = min(new_interval_ns, 10 * RT_NS_1MS);

		if (new_interval_ns != current_interval_ns) {
			RTTimerChangeInterval(pVM->tm.s.pTimer, new_interval_ns);
			current_interval_ns = new_interval_ns;
		}
	}

	Vm &vm = *static_cast<Vm *>(pVM);

	/* commit on VM entry */
	nem_ptr->commit_range();

	VBOXSTRICTRC result = 0;
	vm.with_vcpu(Cpu_index { pVCpu->idCpu }, [&] (Sup::Vcpu &vcpu) {
		result = vcpu.run(); });

	return result;
}


bool nemR3NativeNotifyDebugEventChanged(PVM pVM, bool fUseDebugLoop) STOP


bool nemR3NativeNotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu, bool fUseDebugLoop) STOP


bool NEMR3CanExecuteGuest(PVM pVM, PVMCPU pVCpu)
{
	return true;
}


bool nemR3NativeSetSingleInstruction(PVM pVM, PVMCPU pVCpu, bool fEnable) TRACE(false)


/**
 * Forced flag notification call from VMEmt.h.
 *
 * This is only called when pVCpu is in the VMCPUSTATE_STARTED_EXEC_NEM state.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the CPU
 *                          to be notified.
 * @param   fFlags          Notification flags
 *                          (VMNOTIFYFF_FLAGS_DONE_REM/VMNOTIFYFF_FLAGS_POKE)
 */
void nemR3NativeNotifyFF(PVM pVM, PVMCPU pVCpu, ::uint32_t fFlags)
{
	/* nemHCWinCancelRunVirtualProcessor(pVM, pVCpu); */
	if (fFlags & VMNOTIFYFF_FLAGS_POKE) {
		Sup::Vm &vm = *(Sup::Vm *)pVM;

		vm.with_vcpu(Sup::Cpu_index { pVCpu->idCpu }, [&] (Sup::Vcpu &vcpu) {
			vcpu.pause(); });
	}
}


static void update_pgm_page(PVM pVM, addr_t guest_addr, addr_t host_addr,
                            uint32_t page_id, unsigned count)
{
	/* init all pages in large page (see PGMR3PhysAllocateLargeHandyPage()) */
	for (unsigned i = 0; i < count; ++i) {

		PPGMPAGE page = nullptr;

		pgmPhysGetPageEx(pVM, guest_addr, &page);

		/*
		 * We are called from pgmR3PhysInitAndLinkRamRange (PGMPhys.cpp) with
		 * VBOX_WITH_PGM_NEM_MODE set via NEMR3NotifyPhysRamRegister (with
		 * VBOX_WITH_NATIVE_NEM set and PGM_IS_IN_NEM_MODE set
		 */
		if (PGM_PAGE_GET_TYPE(page) != PGMPAGETYPE_RAM)
			error(__func__, ": page is not RAM");
		if (!PGM_PAGE_IS_ALLOCATED(page)) {
			error(__func__, ": page is not allocated: ", page, " state: ", (unsigned)(page->s.uStateY & 0x7));
			Genode::backtrace();
			STOP;
		}

		PGM_PAGE_SET_HCPHYS(pVM, page, host_addr);
		PGM_PAGE_SET_PAGEID(pVM, page, page_id);
		PGM_PAGE_SET_PDE_TYPE(pVM, page, PGM_PAGE_PDE_TYPE_PDE);
		PGM_PAGE_SET_PTE_INDEX(pVM, page, 0);
		PGM_PAGE_SET_TRACKING(pVM, page, 0);

		page_id++;

		host_addr  += X86_PAGE_4K_SIZE;
		guest_addr += X86_PAGE_4K_SIZE;
	}
}


template <typename T>
static constexpr bool aligned_2M(T value) { return aligned(value, { .log2 = 21 }); }


static
void for_each_vmm_addr(RTGCPHYS GCPhys, RTGCPHYS cb, void *pvR3, auto const &fn)
{
	using Vmm_addr = Sup::Gmm::Vmm_addr;

	addr_t vmm_addr   = addr_t(pvR3);
	addr_t guest_addr = addr_t(GCPhys);

	size_t addend = size_t(_4K);

	/*
	 * Try to map 2MB pages
	 *
	 * Ranges within the first 2 MiB are mapped 4KB to prevent  errors with ROM
	 * mappings below 1 MiB. Also, a range of 64 KiB at 1 MiB is replaced
	 * regularly on A20 switching. Both facts invalidate our large-page mapping.
	 */
	if (aligned_2M(GCPhys) && aligned_2M(vmm_addr) &&
	    cb >= _2M && GCPhys >= _2M)
		addend = size_t(_2M);

	auto loop = [&](size_t addend) {
		for (; vmm_addr + addend <= addr_t(pvR3) + cb;
		       vmm_addr   += addend,
		       guest_addr += addend)
			fn(Vmm_addr { vmm_addr }, guest_addr, addend);
	};

	loop(addend);
	loop(size_t(_4K));
}


/**
 * NEM is notified about each RAM range by calling this function repeatedly
 *
 * PGMR3PhysRegisterRam() holds the PGM lock while calling.
 */
int NEMR3NotifyPhysRamRegister(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvR3,
                               ::uint8_t *pu2State, ::uint32_t *puNemRange)

{
	/*
	 * PGM notifies us about each RAM range configured, which means "Base RAM"
	 * below 4 GiB and "Above 4GB Base RAM" (see MMR3InitPaging()). We eagerly
	 * map all 2M-aligened "large" pages in the ranges to guest memory and
	 * initialize PGM to benefit from reduced TLB usage and less backing store
	 * for many mapped regions.
	 */
	for_each_vmm_addr(GCPhys, cb, pvR3, [&](Sup::Gmm::Vmm_addr const vmm_addr,
	                                        addr_t             const guest_addr,
	                                        size_t             const size) {

		Sup::Gmm::Page_id const vmm_page_id = nem_ptr->gmm().page_id(vmm_addr);
		uint32_t          const page_id32   = nem_ptr->gmm().page_id_as_uint32(vmm_page_id);

		Sup::Nem::Protection const prot { true, true, true };

		nem_ptr->map_to_guest(vmm_addr.value, guest_addr, size, prot);

		update_pgm_page(pVM, guest_addr, vmm_addr.value, page_id32, size / _4K);
	});

	/* invalidate PGM caches (see pgmPhysAllocPage()) */
	PGM_INVL_ALL_VCPU_TLBS(pVM);
	pgmPhysInvalidatePageMapTLB(pVM, false);

	return VINF_SUCCESS;
}


int nemR3NativeNotifyPhysMmioExMap(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb,
                                   ::uint32_t fFlags, void *pvMmio2)
{
	/*
	 * This is called from PGMPhys.cpp with
	 *
	 * fFlags = (pFirstMmio->fFlags & PGMREGMMIO2RANGE_F_MMIO2       ? NEM_NOTIFY_PHYS_MMIO_EX_F_MMIO2   : 0)
	 *        | (pFirstMmio->fFlags & PGMREGMMIO2RANGE_F_OVERLAPPING ? NEM_NOTIFY_PHYS_MMIO_EX_F_REPLACE : 0);
	 */

	return VINF_SUCCESS;
}


int NEMR3NotifyPhysMmioExMapEarly(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, ::uint32_t fFlags,
                                  void *pvRam, void *pvMmio2, ::uint8_t *pu2State, 
                                  ::uint32_t *puNemRange)
{
	*pu2State = (fFlags & NEM_NOTIFY_PHYS_MMIO_EX_F_REPLACE) ? UINT8_MAX : NEM_WIN_PAGE_STATE_UNMAPPED;
	return VINF_SUCCESS;
}


int NEMR3NotifyPhysMmioExMapLate(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, ::uint32_t fFlags,
                                 void *pvRam, void *pvMmio2, ::uint32_t *puNemRange)
{
	return VINF_SUCCESS;
}


int NEMR3NotifyPhysMmioExUnmap(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, ::uint32_t fFlags,
                               void *pvRam, void *pvMmio2, ::uint8_t *pu2State)
{
	if (pu2State) *pu2State = UINT8_MAX;
	return VINF_SUCCESS;
}


int NEMR3NotifyPhysMmioExUnmap(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t fFlags, void *pvRam,
                               void *pvMmio2, ::uint8_t *pu2State, ::uint32_t *puNemRange)
{
	if (pu2State) *pu2State = UINT8_MAX;
	return VINF_SUCCESS;
}


int NEMR3PhysMmio2QueryAndResetDirtyBitmap(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, ::uint32_t uNemRange,
                                           void *pvBitmap, size_t cbBitmap) STOP


bool NEMR3IsMmio2DirtyPageTrackingSupported(PVM pVM)
{
	RT_NOREF(pVM);
	return false;
}


int NEMR3NotifyPhysRomRegisterEarly(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvPages,
                                    ::uint32_t fFlags, ::uint8_t *pu2State, ::uint32_t *puNemRange)

{
	return VINF_SUCCESS;
}

int NEMR3NotifyPhysRomRegisterLate(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, void *pvPages,
                                   ::uint32_t fFlags, ::uint8_t *pu2State, ::uint32_t *puNemRange)
{
	return VINF_SUCCESS;
}


/**
 * Called when the A20 state changes.
 *
 * Do a very minimal emulation of the HMA to make DOS happy.
 *
 * @param   pVCpu           The CPU the A20 state changed on.
 * @param   fEnabled        Whether it was enabled (true) or disabled.
 */
void NEMR3NotifySetA20(PVMCPU pVCpu, bool fEnabled)
{
	/*
	 * Note: pVM->nem.s.fA20Enabled is set to fEnabled before this function is
	 * called, checking it against fEnabled to detect changes doesn't work
	 */
	PVM pVM = pVCpu->CTX_SUFF(pVM);

	/*
	 * unmap/map HMA guest memory when A20 is disabled/enabled
	 *
	 * The unmap is necessary so the instruction emulator is called and can
	 * emulate A20 gate behavior in case the gate disabled (bit 20 is hardwired to
	 * zero).
	 */
	Sup::Nem::Protection const prot {
		.readable   = fEnabled,
		.writeable  = fEnabled,
		.executable = fEnabled,
	};

	for (RTGCPHYS GCPhys = _1M; GCPhys < _1M + _1M; GCPhys += X86_PAGE_SIZE) {

		PPGMPAGE page = nullptr;
		pgmPhysGetPageEx(pVM, GCPhys, &page);

		nem_ptr->map_page_to_guest(addr_t(PGM_PAGE_GET_HCPHYS(page)), GCPhys, prot);
	}
}


void NEMHCNotifyHandlerPhysicalDeregister(PVMCC pVM, PGMPHYSHANDLERKIND enmKind,
                                          RTGCPHYS GCPhys, RTGCPHYS cb,
                                          RTR3PTR pvMemR3, uint8_t *pu2State)
{
}


void NEMHCNativeNotifyHandlerPhysicalModify(PVMCC pVM, PGMPHYSHANDLERKIND enmKind,
                                            RTGCPHYS GCPhysOld, RTGCPHYS GCPhysNew,
                                            RTGCPHYS cb, bool fRestoreAsRAM) STOP


void NEMHCNotifyPhysPageProtChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys,
                                    RTR3PTR pvR3, ::uint32_t fPageProt,
                                    PGMPAGETYPE enmType, ::uint8_t *pu2State)
{
	Sup::Nem::Protection const prot {
		.readable   = (fPageProt & NEM_PAGE_PROT_READ) != 0,
		.writeable  = (fPageProt & NEM_PAGE_PROT_WRITE) != 0,
		.executable = (fPageProt & NEM_PAGE_PROT_EXECUTE) != 0,
	};

	/* page is not mapped yet and has no HCPhys address ROM || MMIO2 */
	if (HCPhys == 0x0000fffffffff000 || HCPhys == 0xffffffff0000) {

		/* map in GMM */
		Sup::Gmm::Vmm_addr const vmm_addr { addr_t(pvR3) };
		Sup::Gmm::Page_id  const vmm_page_id = nem_ptr->gmm().page_id(vmm_addr);
		uint32_t           const page_id32   = nem_ptr->gmm().page_id_as_uint32(vmm_page_id);

		nem_ptr->map_page_to_guest(vmm_addr.value, GCPhys, prot);

		PPGMPAGE page = nullptr;
		pgmPhysGetPageEx(pVM, GCPhys, &page);

		/*
		 * We have only seen this with ROM's so far via pgmR3PhysRomRegisterLocked
		 * and MMIO2 via pgmPhysMmio2RegisterWorker
		 */
		if (PGM_PAGE_GET_TYPE(page) != PGMPAGETYPE_ROM &&
		    PGM_PAGE_GET_TYPE(page) != PGMPAGETYPE_ROM_SHADOW &&
		    PGM_PAGE_GET_TYPE(page) != PGMPAGETYPE_MMIO2)
			error(__func__, " un-mapped page with no host address is not ROM or MMIO2 (",
			      unsigned(PGM_PAGE_GET_TYPE(page)), ")");

		PGM_PAGE_SET_HCPHYS(pVM, page, vmm_addr.value);
		PGM_PAGE_SET_STATE(pVM, page,  PGM_PAGE_STATE_ALLOCATED);
		PGM_PAGE_SET_PDE_TYPE(pVM, page, PGM_PAGE_PDE_TYPE_DONTCARE);
		PGM_PAGE_SET_PTE_INDEX(pVM, page, 0);
		PGM_PAGE_SET_TRACKING(pVM, page, 0);

		/* MMIO2 pages use PGM_MMIO2_PAGEID_MAKE for the creation page ids */
		if (PGM_PAGE_GET_TYPE(page) != PGMPAGETYPE_MMIO2)
			PGM_PAGE_SET_PAGEID(pVM, page, page_id32);

		/*
		 * TODO: check if this is necessary here
		 *
		 * invalidate PGM caches (see pgmPhysAllocPage())
		 */
		PGM_INVL_ALL_VCPU_TLBS(pVM);
		pgmPhysInvalidatePageMapTLB(pVM, false);

		return;
	}

	/*
	 * The passed host and guest addresses may not be aligned, e.g., when
	 * called from DevVGA.cpp vgaLFBAccess(). Therefore, we do the alignment
	 * here explicitly.
	 */
	nem_ptr->map_page_to_guest(HCPhys & ~PAGE_OFFSET_MASK,
	                           GCPhys & ~PAGE_OFFSET_MASK, prot);
}


int nemHCNativeNotifyPhysPageAllocated(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys,
                                       ::uint32_t fPageProt, PGMPAGETYPE enmType,
                                       ::uint8_t *pu2State)
{
	NEMHCNotifyPhysPageProtChanged(pVM, GCPhys, HCPhys, nullptr,
	                               fPageProt, enmType, pu2State);

	return VINF_SUCCESS;
}


void NEMHCNotifyPhysPageChanged(PVMCC pVM, RTGCPHYS GCPhys, RTHCPHYS HCPhysPrev,
                                RTHCPHYS HCPhysNew, RTR3PTR pvNewR3,
                                ::uint32_t fPageProt, PGMPAGETYPE enmType,
                                ::uint8_t *pu2State)

{
	NEMHCNotifyPhysPageProtChanged(pVM, GCPhys, HCPhysNew, pvNewR3, fPageProt, enmType, pu2State);
}


/*
 * XXX: Double check
 * NEM backend uses nested paging for the guest.
 * NEM_FEAT_F_NESTED_PAGING    RT_BIT(0)
 * NEM backend uses full (unrestricted) guest execution.
 * NEM_FEAT_F_FULL_GST_EXEC    RT_BIT(1)
 * NEM backend offers an xsave/xrstor interface.
 * NEM_FEAT_F_XSAVE_XRSTOR
 */
VMM_INT_DECL(uint32_t) NEMHCGetFeatures(PVMCC pVM)
{
	RT_NOREF(pVM);
	return NEM_FEAT_F_NESTED_PAGING | NEM_FEAT_F_FULL_GST_EXEC |
	       NEM_FEAT_F_XSAVE_XRSTOR;
}
