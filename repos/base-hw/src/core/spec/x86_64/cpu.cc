/*
 * \brief   Kernel backend for protection domains
 * \author  Stefan Kalkowski
 * \date    2015-03-20
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <cpu.h>
#include <kernel/thread.h>
#include <kernel/pd.h>

extern int __idt;
extern int __idt_end;

using Cpu = Board::Cpu;


/**
 * Pseudo Descriptor
 *
 * See Intel SDM Vol. 3A, section 3.5.1
 */
struct Pseudo_descriptor
{
	uint16_t const limit = 0;
	uint64_t const base  = 0;

	constexpr Pseudo_descriptor(uint16_t l, uint64_t b) : limit(l), base(b) {}

} __attribute__((packed));


void Cpu::Context::print(Output &output) const
{
	using namespace Genode;
	using Genode::print;

	print(output, "\n");
	print(output, "  ip     = ", Hex(ip),     "\n");
	print(output, "  sp     = ", Hex(sp),     "\n");
	print(output, "  cs     = ", Hex(cs),     "\n");
	print(output, "  ss     = ", Hex(ss),     "\n");
	print(output, "  eflags = ", Hex(eflags), "\n");
	print(output, "  rax    = ", Hex(rax),    "\n");
	print(output, "  rbx    = ", Hex(rbx),    "\n");
	print(output, "  rcx    = ", Hex(rcx),    "\n");
	print(output, "  rdx    = ", Hex(rdx),    "\n");
	print(output, "  rdi    = ", Hex(rdi),    "\n");
	print(output, "  rsi    = ", Hex(rsi),    "\n");
	print(output, "  rbp    = ", Hex(rbp));
}


Cpu::Context::Context(bool core)
{
	eflags = EFLAGS_IF_SET;
	cs     = core ? 0x8 : 0x1b;
	ss     = core ? 0x10 : 0x23;
}


static bool xsave_avail()
{
	using Id = Cpu::Cpuid_1_ecx;
	static bool avail = Id::Xsave::get(Id::read());
	return avail;
}


static bool xsaves_avail()
{
	using Id = Cpu::Cpuid_d_1_eax;
	static bool avail = Id::Xsaves::get(Id::read());
	return avail;
}

static bool xsaveopt_avail()
{
	using Id = Cpu::Cpuid_d_1_eax;
	static bool avail = Id::Xsaveopt::get(Id::read());
	return avail;
}

static uint32_t xcr0_low()
{
	static constexpr uint32_t hw_supported =
		Cpu::Xstate_components::X87::bits(1) |
		Cpu::Xstate_components::Sse::bits(1) |
		Cpu::Xstate_components::Avx::bits(1) |
		Cpu::Xstate_components::Avx_512::bits(0b111);
	static uint32_t xcr0 = Cpu::Cpuid_xcr0_low::read();
	return xcr0 & hw_supported;
}


Cpu::Fpu::Fpu()
{
	if (!xsave_avail())
		return;

	Cpu::Cr4::access_t cr4 = Cpu::Cr4::read();
	Cpu::Cr4::Osxsave::set(cr4, 1);
	Cpu::Cr4::write(cr4);

	/* we don't make use of the extended supervisor state save/restore */
	if (xsaves_avail()) Cpu::Ia32_xss::write(0);

	Cpu::Xcr0::write(xcr0_low());

	if (Cpu::Cpuid_xsave_bytes_enabled::read() > Cpu::Fpu_context::SIZE)
		error("XSAVE state is bigger than kernel's specified size!");
}


Cpu::Fpu_context::Fpu_context()
{
	Context init({ _data, SIZE });
	init.write<Context::Fpu_control>(0x37f);    /* mask exceptions SysV ABI */
	init.write<Context::Simd_control_status>(0x1f80);
	if (xsaves_avail())
		init.write<Context::Xcomp>(Context::Xcomp::Compact::bits(1));
}


void Cpu::Fpu_context::save()
{
	if (!xsave_avail()) {
		asm volatile("fxsave (%0)" :: "r" (this));
		return;
	}

	if (xsaves_avail()) {
		asm volatile ("xsaves64 (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0_low()) : "memory");
		return;
	}

	if (xsaveopt_avail())
		asm volatile ("xsaveopt (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0_low()) : "memory");
	else
		asm volatile ("xsave64 (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0_low()) : "memory");
}


void Cpu::Fpu_context::load() const
{
	if (!xsave_avail()) {
		asm volatile("fxrstor (%0)" :: "r" (this));
		return;
	}

	if (xsaves_avail())
		asm volatile ("xrstors64 (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0_low()) : "memory");
	else
		asm volatile ("xrstor64 (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0_low()) : "memory");
}


Cpu::Mmu_context::Mmu_context(addr_t table, addr_t id)
:
	cr3(Cr3::Pdb::masked(table))
{
	if (pcid_avail()) Cr3::Pcid::set(cr3, id);
}


void Cpu::Tss::init()
{
	enum { TSS_SELECTOR = 0x28, };
	asm volatile ("ltr %w0" : : "r" (TSS_SELECTOR));
}


void Cpu::Idt::init()
{
	Pseudo_descriptor descriptor {
		(uint16_t)((addr_t)&__idt_end - (addr_t)&__idt),
		(uint64_t)(&__idt) };
	asm volatile ("lidt %0" : : "m" (descriptor));
}


void Cpu::Gdt::init(addr_t tss_addr)
{
	tss_desc[0] = ((((tss_addr >> 24) & 0xff) << 24 |
	                ((tss_addr >> 16) & 0xff)       |
	               0x8900) << 32)                   |
	              ((tss_addr &  0xffff) << 16 | 0x68);
	tss_desc[1] = tss_addr >> 32;

	Pseudo_descriptor descriptor {
		(uint16_t)(sizeof(Gdt)),
		(uint64_t)(this) };
	asm volatile ("lgdt %0" :: "m" (descriptor));
}


void Cpu::mmu_fault(Cpu_state &state, Kernel::Thread_fault &fault)
{
	using Fault = Kernel::Thread_fault::Type;

	/*
	 * Intel manual: 6.15 EXCEPTION AND INTERRUPT REFERENCE
	 *                    Interrupt 14—Page-Fault Exception (#PF)
	 */
	enum {
		ERR_I = 1UL << 4,
		ERR_R = 1UL << 3,
		ERR_U = 1UL << 2,
		ERR_W = 1UL << 1,
		ERR_P = 1UL << 0,
	};

	auto fault_lambda = [] (addr_t err) {
		if (err & ERR_W)    return Fault::WRITE;
		if (!(err & ERR_P)) return Fault::PAGE_MISSING;
		if (err & ERR_I)    return Fault::EXEC;
		else                return Fault::UNKNOWN;
	};

	fault.addr = Cpu::Cr2::read();
	fault.type = fault_lambda(state.errcode);
}


bool Cpu::active(Mmu_context &mmu_context)
{
	return (mmu_context.cr3 == Cr3::read());
}


void Cpu::switch_to(Mmu_context &mmu_context)
{
	Cr3::access_t cr3 = mmu_context.cr3;
	if (pcid_avail()) Cr3::Tlb_ignore::set(cr3, 1);
	Cr3::write(cr3);
}


Cpu::Id Cpu::executing_id()
{
	return { Cpu::Cpuid_1_ebx::Apic_id::get(Cpu::Cpuid_1_ebx::read()) };
}


void Cpu::clear_memory_region(addr_t const addr, size_t const size, bool)
{
	Align const AT_8 { .log2 = 3 };
	if (align_addr(addr, AT_8) == addr && align_addr(size, AT_8) == size) {
		addr_t start = addr;
		size_t count = size / 8;
		asm volatile ("rep stosq" : "+D" (start), "+c" (count)
		                          : "a" (0)  : "memory");
	} else {
		bzero((void*)addr, size);
	}
}


void Cpu::single_step(Context &regs, bool on)
{
	if (on)
		regs.eflags |= Context::Eflags::EFLAGS_TF;
	else
		regs.eflags &= ~Context::Eflags::EFLAGS_TF;
}


bool Cpu::pcid_avail()
{
	static bool avail = Cpuid_1_ecx::Pcid::get(Cpuid_1_ecx::read());
	return avail;
}


void Cpu::invalidate_tlb(Mmu_context &mmu_context, addr_t addr, size_t size, bool core)
{
	/* non global entries get deleted by CR3 re-loading */
	if (!core) {
		if (pcid_avail()) {
			Cr3::access_t cr3 = Cr3::read();
			Cr3::write(mmu_context.cr3);
			if (pcid_avail()) Cr3::Tlb_ignore::set(cr3, 1);
			Cr3::write(cr3);
		} else {
			Cr3::write(Cr3::read());
		}
		return;
	}

	/*
	 * if the size of the virtual region is too big,
	 * calling invlpg for each page-entry gets too expensive,
	 * just flush everything then.
	 */
	if (size > 32 * PAGE_SIZE) {
		Cr4::access_t cr4 = Cr4::read();
		Cr4::Pge::set(cr4, 0);
		Cr4::write(cr4);
		Cr4::Pge::set(cr4, 1);
		Cr4::write(cr4);
		return;
	}

	for (addr_t page = addr; page < (addr+size); page += PAGE_SIZE)
		asm volatile ("invlpg (%0)" :: "r" (page) : "memory");
}


bool Cpu::user_msr_read(addr_t const msr, addr_t &value)
{
	uint32_t msr_addr = msr & 0xffffffff;

	static unsigned const family = Hw::Vendor::get_family();
	static unsigned const model  = Hw::Vendor::get_model();

	bool const nehalem_or_newer     = (family == 0x6);
	bool const sandybridge_or_newer = (family == 0x6) && (model >= 0x2a);
	bool const haswell_or_newer     = (family == 0x6) && (model >= 0x3c);
	bool const cannonlake           = (family == 0x6) && (model == 0x66);

	value = 0;

	switch(msr_addr) {
	case IA32_APERF:
	case IA32_MPERF:
		{
			using Id = Cpuid_power_thermal_ecx;
			if (!Id::Mperf_aperf::get(Id::read()))
				return false;
			break;
		}
	case IA32_THERM_STATUS:
		{
			if (!Cpuid_1_edx::Acpi::get(Cpuid_1_edx::read()))
				return false;
			break;
		}
	case IA32_PACKAGE_THERM_STATUS:
		{
			using Id = Cpuid_power_thermal_eax;
			if (!Id::Pkg_therm_mgmt::get(Id::read()))
				return false;
			break;
		}
	case IA32_PM_ENABLE:
	case IA32_HWP_CAPABILITIES:
	case IA32_HWP_REQUEST:
		{
			using Id = Cpuid_power_thermal_eax;
			if (!Id::Hwp::get(Id::read()))
				return false;
			break;
		}
	case IA32_HWP_REQUEST_PKG:
		{
			using Id = Cpuid_power_thermal_eax;
			if (!Id::Hwp_request_pkg::get(Id::read()))
				return false;
			break;
		}
	case IA32_ENERGY_PERF_BIAS:
		{
			using Id = Cpuid_power_thermal_ecx;
			if (!Id::Energy_perf_bias::get(Id::read()))
				return false;
			break;
		}
	case MSR_TEMPERATURE_TARGET:
	case MSR_PKG_C3_RESIDENCY:
	case MSR_PKG_C6_RESIDENCY:
	case MSR_PKG_C7_RESIDENCY:
	case MSR_CORE_C3_RESIDENCY:
	case MSR_CORE_C6_RESIDENCY:
		if (nehalem_or_newer)
			break;
		return false;
	case MSR_CORE_C7_RESIDENCY:
	case MSR_RAPL_POWER_UNIT:
	case MSR_PKG_C2_RESIDENCY:
	case MSR_PKG_ENERGY_STATUS:
	case MSR_PP0_POWER_LIMIT:
	case MSR_PP0_ENERGY_STATUS:
	case MSR_PP0_POLICY:
	case MSR_PP1_POWER_LIMIT:
	case MSR_PP1_ENERGY_STATUS:
	case MSR_PP1_POLICY:
	case MSR_PKG_POWER_INFO:
	case MSR_PKG_POWER_LIMIT:
		if (sandybridge_or_newer)
			break;
		return false;
	case MSR_DRAM_ENERGY_STATUS:
	case MSR_DRAM_PERF_STATUS:
	case MSR_PKG_PERF_STATUS:
	case MSR_PKG_C8_RESIDENCY:
	case MSR_PKG_C9_RESIDENCY:
	case MSR_PKG_C10_RESIDENCY:
		if (haswell_or_newer)
			break;
		return false;
	case MSR_CORE_C1_RESIDENCY:
		if (cannonlake)
			break;
		return false;
	default:
		return false;
	};

	uint32_t low, high;
	asm volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr_addr));
	value = ((uint64_t)high << 32) | (low & ~0U);
	return true;
}


bool Cpu::user_msr_write(addr_t const msr, addr_t const value)
{
	uint32_t msr_addr = msr & 0xffffffff;

	switch(msr_addr) {
	case IA32_PM_ENABLE:
	case IA32_HWP_REQUEST:
		{
			using Id = Cpuid_power_thermal_eax;
			if (!Id::Hwp::get(Id::read()))
				return false;
			break;
		}
	case IA32_HWP_REQUEST_PKG:
		{
			using Id = Cpuid_power_thermal_eax;
			if (!Id::Hwp_request_pkg::get(Id::read()))
				return false;
			break;
		}
	case IA32_ENERGY_PERF_BIAS:
		{
			using Id = Cpuid_power_thermal_ecx;
			if (!Id::Energy_perf_bias::get(Id::read()))
				return false;
			break;
		}
	default:
		return false;
	};

	asm volatile ("wrmsr" :: "a" (value), "d" (value>>32), "c" (msr_addr));
	return true;
}
