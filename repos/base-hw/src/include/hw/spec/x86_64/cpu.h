/*
 * \brief  x86_64 CPU definitions
 * \author Stefan Kalkowski
 * \author Benjamin Lamowski
 * \date   2017-04-07
 */

/*
 * Copyright (C) 2017-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__LIB__HW__SPEC__X86_64__CPU_H_
#define _SRC__LIB__HW__SPEC__X86_64__CPU_H_

#include <hw/id.h>
#include <hw/spec/x86_64/register_macros.h>

namespace Hw { struct X86_64_cpu; struct Suspend_type; }


/*
 * The intended sleep state S0 ... S5. The values are read out by an
 * ACPI AML component and are of type TYP_SLPx as described in the
 * ACPI specification, e.g. TYP_SLPa and TYP_SLPb. The values differ
 * between different PC systems/boards.
 */
struct Hw::Suspend_type {
	Genode::uint8_t typ_a;
	Genode::uint8_t typ_b;
};


struct Hw::X86_64_cpu
{
	/**
	 * We use the Cpuid_1_ebx::Apic_id as identifier,
	 * which has size of one byte
	 */
	using Id = Hw::Id<Genode::uint8_t>;

	X86_64_CR_REGISTER(Cr0, cr0,
		struct Pe : Bitfield< 0, 1> { }; /* Protection Enable   */
		struct Mp : Bitfield< 1, 1> { }; /* Monitor Coprocessor */
		struct Em : Bitfield< 2, 1> { }; /* Emulation           */
		struct Ts : Bitfield< 3, 1> { }; /* Task Switched       */
		struct Et : Bitfield< 4, 1> { }; /* Extension Type      */
		struct Ne : Bitfield< 5, 1> { }; /* Numeric Error       */
		struct Wp : Bitfield<16, 1> { }; /* Write Protect       */
		struct Am : Bitfield<18, 1> { }; /* Alignment Mask      */
		struct Nw : Bitfield<29, 1> { }; /* Not Write-through   */
		struct Cd : Bitfield<30, 1> { }; /* Cache Disable       */
		struct Pg : Bitfield<31, 1> { }; /* Paging              */
	);

	/**
	 * Control register 2: Page-fault linear address
	 *
	 * See Intel SDM Vol. 3A, section 2.5.
	 */
	X86_64_CR_REGISTER(Cr2, cr2,
		struct Addr : Bitfield<0, 63> { };
	);

	/**
	 * Control register 3: Page-Directory base register
	 *
	 * See Intel SDM Vol. 3A, section 2.5 and section 5.
	 */
	X86_64_CR_REGISTER(Cr3, cr3,
		struct Pcid : Bitfield<0,  12> { }; /* Process-context ID */
		struct Pdb  : Bitfield<12, 36> { }; /* Page-directory base address */
		struct Tlb_ignore : Bitfield<63, 1> { }; /* if PCID is enabled */
	);

	X86_64_CR_REGISTER(Cr4, cr4,
		struct Vme        : Bitfield< 0, 1> { }; /* Virtual-8086 Mode Extensions */
		struct Pvi        : Bitfield< 1, 1> { }; /* Protected-Mode Virtual IRQs */
		struct Tsd        : Bitfield< 2, 1> { }; /* Time Stamp Disable */
		struct De         : Bitfield< 3, 1> { }; /* Debugging Exceptions */
		struct Pse        : Bitfield< 4, 1> { }; /* Page Size Extensions */
		struct Pae        : Bitfield< 5, 1> { }; /* Physical Address Extension */
		struct Mce        : Bitfield< 6, 1> { }; /* Machine-Check Enable */
		struct Pge        : Bitfield< 7, 1> { }; /* Page Global Enable */
		struct Pce        : Bitfield< 8, 1> { }; /* Performance-Monitoring Counter
		                                            Enable*/
		struct Osfxsr     : Bitfield< 9, 1> { }; /* OS Support for FXSAVE and
		                                            FXRSTOR instructions*/
		struct Osxmmexcpt : Bitfield<10, 1> { }; /* OS Support for Unmasked
		                                            SIMD/FPU Exceptions */
		struct Vmxe       : Bitfield<13, 1> { }; /* VMX Enable */
		struct Smxe       : Bitfield<14, 1> { }; /* SMX Enable */
		struct Fsgsbase   : Bitfield<16, 1> { }; /* FSGSBASE-Enable */
		struct Pcide      : Bitfield<17, 1> { }; /* PCID Enable */
		struct Osxsave    : Bitfield<18, 1> { }; /* XSAVE and Processor Extended
		                                            States-Enable */
		struct Smep       : Bitfield<20, 1> { }; /* SMEP Enable */
		struct Smap       : Bitfield<21, 1> { }; /* SMAP Enable */
	);

	X86_64_XCR_REGISTER(Xcr0, 0);

	enum Msr {
		IA32_PLATFORM_ID             = 0x17,
		IA32_APIC_BASE               = 0x1b,
		IA32_FEATURE_CONTROL         = 0x3a,
		MSR_FSB_FREQ                 = 0xcd,
		MSR_PLATFORM_INFO            = 0xce,
		IA32_MPERF                   = 0xe7,
		IA32_APERF                   = 0xe8,
		IA32_THERM_STATUS            = 0x19c,
		MSR_TEMPERATURE_TARGET       = 0x1a2,
		IA32_ENERGY_PERF_BIAS        = 0x1b0,
		IA32_PACKAGE_THERM_STATUS    = 0x1b1,
		IA32_PAT                     = 0x277,
		MSR_PKG_C3_RESIDENCY         = 0x3f8,
		MSR_PKG_C6_RESIDENCY         = 0x3f9,
		MSR_PKG_C7_RESIDENCY         = 0x3fa,
		MSR_CORE_C3_RESIDENCY        = 0x3fc,
		MSR_CORE_C6_RESIDENCY        = 0x3fd,
		MSR_CORE_C7_RESIDENCY        = 0x3fe,
		IA32_VMX_BASIC               = 0x480,
		IA32_VMX_PINBASED_CTLS       = 0x481,
		IA32_VMX_PROCBASED_CTLS      = 0x482,
		IA32_VMX_EXIT_CTLS           = 0x483,
		IA32_VMX_ENTRY_CTLS          = 0x484,
		IA32_VMX_CR0_FIXED0          = 0x486,
		IA32_VMX_CR0_FIXED1          = 0x487,
		IA32_VMX_CR4_FIXED0          = 0x488,
		IA32_VMX_CR4_FIXED1          = 0x489,
		IA32_VMX_PROCBASED_CTLS2     = 0x48b,
		IA32_VMX_TRUE_PINBASED_CTLS  = 0x48d,
		IA32_VMX_TRUE_PROCBASED_CTLS = 0x48e,
		IA32_VMX_TRUE_EXIT_CTLS      = 0x48f,
		IA32_VMX_TRUE_ENTRY_CTLS     = 0x490,
		MSR_RAPL_POWER_UNIT          = 0x606,
		MSR_PKG_C2_RESIDENCY         = 0x60d,
		MSR_PKG_POWER_LIMIT          = 0x610,
		MSR_PKG_ENERGY_STATUS        = 0x611,
		MSR_PKG_PERF_STATUS          = 0x613,
		MSR_PKG_POWER_INFO           = 0x614,
		MSR_DRAM_ENERGY_STATUS       = 0x619,
		MSR_DRAM_PERF_STATUS         = 0x61b,
		MSR_PKG_C8_RESIDENCY         = 0x630,
		MSR_PKG_C9_RESIDENCY         = 0x631,
		MSR_PKG_C10_RESIDENCY        = 0x632,
		MSR_PP0_POWER_LIMIT          = 0x638,
		MSR_PP0_ENERGY_STATUS        = 0x639,
		MSR_PP0_POLICY               = 0x63a,
		MSR_PP1_POWER_LIMIT          = 0x640,
		MSR_PP1_ENERGY_STATUS        = 0x641,
		MSR_PP1_POLICY               = 0x642,
		MSR_CORE_C1_RESIDENCY        = 0x660,
		IA32_PM_ENABLE               = 0x770,
		IA32_HWP_CAPABILITIES        = 0x771,
		IA32_HWP_REQUEST_PKG         = 0x772,
		IA32_HWP_REQUEST             = 0x774,
		IA32_XSS                     = 0xda0,
		IA32_EFER                    = 0xc0000080,
		IA32_FS_BASE                 = 0xc0000100,
		IA32_GS_BASE                 = 0xc0000101,
		IA32_STAR                    = 0xc0000081,
		IA32_LSTAR                   = 0xc0000082,
		IA32_CSTAR                   = 0xc0000083,
		IA32_FMASK                   = 0xc0000084,
		IA32_KERNEL_GS_BASE          = 0xc0000102,
		IA32_TSC_AUX                 = 0xc0000103,
		AMD_VM_SYSCVG                = 0xc0010010,
		AMD_VM_CR                    = 0xc0010114,
		AMD_VM_HSAVEPA               = 0xc0010117,
		AMD_LFENCE                   = 0xc0011029,

	};

	X86_64_MSR_REGISTER(IA32_apic_base, IA32_APIC_BASE,
		struct Bsp    : Bitfield<  8,  1> { }; /* Bootstrap processor */
		struct X2apic : Bitfield< 10,  1> { }; /* Enable/disable X2APIC */
		struct Lapic  : Bitfield< 11,  1> { }; /* Enable/disable local APIC */
		struct Base   : Bitfield< 12, 24> { }; /* Base address of APIC registers */
	);

	X86_64_MSR_REGISTER(IA32_pat, IA32_PAT,
		struct Pa1 : Bitfield <8, 3> {
			enum { WRITE_COMBINING = 0b001 };
		};
	);

	X86_64_MSR_REGISTER(Amd_vm_syscvg, AMD_VM_SYSCVG,
		struct Nested_paging : Bitfield< 0, 1> { }; /* Enable nested paging */
		struct Sev : Bitfield< 1, 1> { }; /* Enable Secure Encrypted Virtualization */
		struct Enc_state : Bitfield< 2, 1> { }; /* Enable Encrypted State for Secure Encrypted Virtualization */
	);

	X86_64_MSR_REGISTER(Amd_vm_cr, AMD_VM_CR,
		struct Svmdis : Bitfield< 4, 1> { }; /* SVM disabled */
	);

	/* AMD host save physical address */
	X86_64_MSR_REGISTER(Amd_vm_hsavepa, AMD_VM_HSAVEPA);


	/* Non-architectural MSR used to make lfence serializing */
	X86_64_MSR_REGISTER(Amd_lfence, AMD_LFENCE,
		struct Enable_dispatch_serializing : Bitfield<1, 1> { }; /* Enable lfence dispatch serializing */
	)

	X86_64_MSR_REGISTER(Platform_id, IA32_PLATFORM_ID,
		struct Bus_ratio : Bitfield<8, 5> { }; /* Bus ratio on Core 2, see SDM 19.7.3 */
	);

	X86_64_MSR_REGISTER(Platform_info, MSR_PLATFORM_INFO,
		struct Ratio : Bitfield< 8, 8> { }; /* Maximum Non-Turbo Ratio (R/O) */
	);

	X86_64_MSR_REGISTER(Fsb_freq, MSR_FSB_FREQ,
		struct Speed : Bitfield< 0, 3> { }; /* Scaleable Bus Speed (R/O) */
	);

	X86_64_MSR_REGISTER(Ia32_efer, IA32_EFER,
		struct Lme  : Bitfield< 8, 1> { }; /* Long Mode Enable */
		struct Lma  : Bitfield<10, 1> { }; /* Long Mode Active */
		struct Svme : Bitfield<12, 1> { }; /* Secure Virtual Machine Enable */
	);

	/* Map of BASE Address of FS */
	X86_64_MSR_REGISTER(Ia32_fs_base, IA32_FS_BASE);

	/* Map of BASE Address of GS */
	X86_64_MSR_REGISTER(Ia32_gs_base, IA32_GS_BASE);

	/* System Call Target Address */
	X86_64_MSR_REGISTER(Ia32_star, IA32_STAR);

	/* IA-32e Mode System Call Target Address */
	X86_64_MSR_REGISTER(Ia32_lstar, IA32_LSTAR);

	/* IA-32e Mode System Call Target Address */
	X86_64_MSR_REGISTER(Ia32_cstar, IA32_CSTAR);

	/* System Call Flag Mask */
	X86_64_MSR_REGISTER(Ia32_fmask, IA32_FMASK);

	/* Swap Target of BASE Address of GS */
	X86_64_MSR_REGISTER(Ia32_kernel_gs_base, IA32_KERNEL_GS_BASE);

	/* See Vol. 4, Table 2-2 of the Intel SDM */
	X86_64_MSR_REGISTER(Ia32_feature_control, IA32_FEATURE_CONTROL,
		struct Lock       : Bitfield< 0, 0> { }; /* VMX Lock */
		struct Vmx_no_smx : Bitfield< 2, 2> { }; /* Enable VMX outside SMX */
	);

	/*
	 * Auxiliary TSC register
	 * For details, see Vol. 3B of the Intel SDM (September 2023):
	 * 18.17.2 IA32_TSC_AUX Register and RDTSCP Support
	 */
	X86_64_MSR_REGISTER(Ia32_tsc_aux, IA32_TSC_AUX);

	/*
	 * Reporting Register of Basic VMX Capabilities
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.1 Basic VMX Information
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_basic, IA32_VMX_BASIC,
		struct Rev             : Bitfield< 0,31> { }; /* VMCS revision */
		struct Clear_controls  : Bitfield<55, 1> { }; /* VMCS controls may be cleared, see A.2 */
	);

	/*
	 * Capability Reporting Register of Pin-Based VM-Execution Controls
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.3.1 Pin-Based VM-Execution Controls
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_pinbased_ctls, IA32_VMX_PINBASED_CTLS,
		struct Allowed_0_settings : Bitfield< 0,32> { }; /* allowed 0-settings */
		struct Allowed_1_settings : Bitfield<32,32> { }; /* allowed 1-settings */
	);

	/*
	 * Capability Reporting Register of Pin-Based VM-Execution Flex Controls
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.3.1 Pin-Based VM-Execution Controls
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_true_pinbased_ctls, IA32_VMX_TRUE_PINBASED_CTLS,
		struct Allowed_0_settings : Bitfield< 0,32> { }; /* allowed 0-settings */
		struct Allowed_1_settings : Bitfield<32,32> { }; /* allowed 1-settings */
	);

	/*
	 * Capability Reporting Register of Primary Processor-Based VM-Execution Controls
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.3.2 Primary Processor-Based VM-Execution Controls
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_procbased_ctls, IA32_VMX_PROCBASED_CTLS,
		struct Allowed_0_settings : Bitfield< 0,32> { }; /* allowed 0-settings */
		struct Allowed_1_settings : Bitfield<32,32> { }; /* allowed 1-settings */
	);

	/*
	 * Capability Reporting Register of Primary Processor-Based VM-Execution Flex Controls
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.3.2 Primary Processor-Based VM-Execution Controls
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_true_procbased_ctls, IA32_VMX_TRUE_PROCBASED_CTLS,
		struct Allowed_0_settings : Bitfield< 0,32> { }; /* allowed 0-settings */
		struct Allowed_1_settings : Bitfield<32,32> { }; /* allowed 1-settings */
	);

	/*
	 * Capability Reporting Register of Primary VM-Exit Controls
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.4.1 Primary VM-Exit Controls
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_exit_ctls, IA32_VMX_EXIT_CTLS,
		struct Allowed_0_settings : Bitfield< 0,32> { }; /* allowed 0-settings */
		struct Allowed_1_settings : Bitfield<32,32> { }; /* allowed 1-settings */
	);

	/*
	 * Capability Reporting Register of VM-Exit Flex Controls
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.4.1 Primary VM-Exit Controls
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_true_exit_ctls, IA32_VMX_TRUE_EXIT_CTLS,
		struct Allowed_0_settings : Bitfield< 0,32> { }; /* allowed 0-settings */
		struct Allowed_1_settings : Bitfield<32,32> { }; /* allowed 1-settings */
	);

	/*
	 * Capability Reporting Register of VM-Entry Controls
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.5 VM-Entry Controls
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_entry_ctls, IA32_VMX_ENTRY_CTLS,
		struct Allowed_0_settings : Bitfield< 0,32> { }; /* allowed 0-settings */
		struct Allowed_1_settings : Bitfield<32,32> { }; /* allowed 1-settings */
	);

	/*
	 * Capability Reporting Register of VM-Entry Flex Controls
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.5 VM-Entry Controls
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_true_entry_ctls, IA32_VMX_TRUE_ENTRY_CTLS,
		struct Allowed_0_settings : Bitfield< 0,32> { }; /* allowed 0-settings */
		struct Allowed_1_settings : Bitfield<32,32> { }; /* allowed 1-settings */
	);

	/*
	 * Capability Reporting Register of Secondary Processor-Based VM-Execution Controls
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.3.3 Secondary Processor-Based VM-Execution Controls
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_procbased_ctls2, IA32_VMX_PROCBASED_CTLS2,
		struct Allowed_0_settings : Bitfield< 0,32> { }; /* allowed 0-settings */
		struct Allowed_1_settings : Bitfield<32,32> { }; /* allowed 1-settings */
	);

	/*
	 * Capability Reporting Register of CR0 Bits Fixed to 0
	 * [sic] in fact, bits reported here need to be 1
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.7 VMX-Fixed Bits in CR0
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_cr0_fixed0, IA32_VMX_CR0_FIXED0);

	/*
	 * Capability Reporting Register of CR0 Bits Fixed to 1
	 * [sic] in fact, bits *NOT* reported here need to be 0
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.7 VMX-Fixed Bits in CR0
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_cr0_fixed1, IA32_VMX_CR0_FIXED1);

	/*
	 * Capability Reporting Register of CR5 Bits Fixed to 0
	 * [sic] in fact, bits reported here need to be 1
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.8 VMX-Fixed Bits in CR4
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_cr4_fixed0, IA32_VMX_CR4_FIXED0);

	/*
	 * Capability Reporting Register of CR4 Bits Fixed to 1
	 * [sic] in fact, bits *NOT* reported here need to be 0
	 * For details, see Vol. 3D of the Intel SDM (September 2023):
	 * A.8 VMX-Fixed Bits in CR4
	 */
	X86_64_MSR_REGISTER(Ia32_vmx_cr4_fixed1, IA32_VMX_CR4_FIXED1);

	X86_64_MSR_REGISTER(Ia32_xss, IA32_XSS);

	X86_64_CPUID_REGISTER(Cpuid_0_eax, 0, 0, eax);
	X86_64_CPUID_REGISTER(Cpuid_0_ebx, 0, 0, ebx);
	X86_64_CPUID_REGISTER(Cpuid_0_ecx, 0, 0, ecx);
	X86_64_CPUID_REGISTER(Cpuid_0_edx, 0, 0, edx);

	X86_64_CPUID_REGISTER(Cpuid_1_eax, 1, 0, eax);

	X86_64_CPUID_REGISTER(Cpuid_1_ebx, 1, 0, ebx,
		struct Apic_id : Bitfield<24, 8> { };
	);

	X86_64_CPUID_REGISTER(Cpuid_1_ecx, 1, 0, ecx,
		struct Vmx          : Bitfield< 5, 1> { };
		struct Pcid         : Bitfield<17, 1> { };
		struct X2apic       : Bitfield<21, 1> { };
		struct Tsc_deadline : Bitfield<24, 1> { };
		struct Xsave        : Bitfield<26, 1> { };
	);

	X86_64_CPUID_REGISTER(Cpuid_1_edx, 1, 0, edx,
		struct Pat  : Bitfield<16, 1> { };
		struct Acpi : Bitfield<22, 1> { };
	);

	X86_64_CPUID_REGISTER(Cpuid_power_thermal_eax, 6, 0, eax,
		struct Pkg_therm_mgmt  : Bitfield<6, 1> { };
		struct Hwp             : Bitfield<7, 1> { };
		struct Hwp_request_pkg : Bitfield<11,1> { };
	);

	X86_64_CPUID_REGISTER(Cpuid_power_thermal_ecx, 6, 0, ecx,
		struct Mperf_aperf      : Bitfield<0,1> { };
		struct Energy_perf_bias : Bitfield<0,3> { };
	);

	X86_64_CPUID_REGISTER(Cpuid_xcr0_low, 0xd, 0, eax);
	X86_64_CPUID_REGISTER(Cpuid_xsave_bytes_enabled, 0xd, 0, ebx);

	X86_64_CPUID_REGISTER(Cpuid_d_1_eax, 0xd, 1, eax,
		struct Xsaveopt : Bitfield<0, 1> { };
		struct Xsaves   : Bitfield<3, 1> { };
	);

	X86_64_CPUID_REGISTER(Cpuid_ia32_xss_low,  0xd, 1, ecx);
	X86_64_CPUID_REGISTER(Cpuid_ia32_xss_high, 0xd, 1, edx);

	X86_64_CPUID_REGISTER(Cpuid_8000000A_edx, 0x8000000A, 0, edx,
		struct Np : Bitfield<0, 1> { }; /* Nested paging */
	);

	X86_64_CPUID_REGISTER(Cpuid_80000007_eax, 0x80000007, 0, eax,
		struct Invariant_tsc : Bitfield<2, 1> { }; /* Invariant TSC */
	);

	X86_64_CPUID_REGISTER(Cpuid_80000001_ecx, 0x80000001, 0, ecx,
		struct Svm : Bitfield<2, 1> { };
	);

	/*
	 * XSAVE feature set comprises different state components,
	 * the following bitmap shows a subset of it that is common
	 * across different registers like Cpuid_xcr0_low, Xcr0,...,
	 * and also used in operations like xsave/xrstor to define
	 * which state is addressed.
	 *
	 * See Intel SDM Vol. 1, section 13.1.
	 */
	struct Xstate_components : Genode::Register<64>
	{
		struct X87     : Bitfield<0, 1> { };
		struct Sse     : Bitfield<1, 1> { };
		struct Avx     : Bitfield<2, 1> { };
		struct Avx_512 : Bitfield<5, 3> { };
	};

	Suspend_type suspend;

	static bool x2apic_support()
	{
		Cpuid_1_ecx::access_t ecx = Cpuid_1_ecx::read();
		return Cpuid_1_ecx::X2apic::get(ecx);
	}
};

#endif /* _SRC__LIB__HW__SPEC__X86_64__CPU_H_ */
