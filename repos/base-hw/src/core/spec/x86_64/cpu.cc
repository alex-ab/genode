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
#include <kernel/cpu.h>
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


Cpu::Context::Context(bool core, Cpu &cpu)
:
	fc(cpu)
{
	eflags = EFLAGS_IF_SET;
	cs     = core ? 0x8 : 0x1b;
	ss     = core ? 0x10 : 0x23;
}


Cpu::Fpu_context::Fpu_context(Cpu &cpu)
:
	xstate_support(cpu.xstate_support), xcr0(cpu.xcr0)
{
	Context init({ _data, SIZE });
	init.write<Context::Fpu_control>(0x37f);    /* mask exceptions SysV ABI */
	init.write<Context::Simd_control_status>(0x1f80);

	if (xstate_support == Xstate_support::XSAVES)
		init.write<Context::Xcomp>(Context::Xcomp::Compact::bits(1));
}


void Cpu::Fpu_context::save()
{
	switch (xstate_support) {
	case Xstate_support::XSAVES:
		asm volatile ("xsaves64 (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0) : "memory");
		return;
	case Xstate_support::XSAVE:
		asm volatile ("xsave64 (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0) : "memory");
		return;
	case Xstate_support::XSAVEOPT:
		asm volatile ("xsaveopt (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0) : "memory");
		return;
	case Xstate_support::LEGACY:
		asm volatile("fxsave (%0)" :: "r" (this));
	}
}


void Cpu::Fpu_context::load() const
{
	switch (xstate_support) {
	case Xstate_support::XSAVES:
		asm volatile ("xrstors64 (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0) : "memory");
		return;
	case Xstate_support::XSAVE:
	case Xstate_support::XSAVEOPT:
		asm volatile ("xrstor64 (%0)"
		              :: "r" (this), "d" (0), "a" (xcr0) : "memory");
		return;
	case Xstate_support::LEGACY:
		asm volatile("fxrstor (%0)" :: "r" (this));
	}
}


Cpu::Mmu_context::Mmu_context(addr_t table, addr_t id)
:
	cr3(Cr3::Pdb::masked(table))
{
	Kernel::Cpu::with_current([&] (auto &cpu) {
		if (cpu.cpuid_1.pcid()) Cr3::Pcid::set(cr3, id); });
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
	if (cpuid_1.pcid()) Cr3::Tlb_ignore::set(cr3, 1);
	Cr3::write(cr3);
}


Cpu::Id Cpu::executing_id()
{
	Cpuid_1 id { 0x1 };
	return { id.apic_id() };
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


void Cpu::invalidate_tlb(Mmu_context &mmu_context, addr_t addr, size_t size, bool core)
{
	/* non global entries get deleted by CR3 re-loading */
	if (!core) {
		if (!cpuid_1.pcid()) {
			Cr3::write(Cr3::read());
			return;
		}

		Cr3::access_t cr3 = Cr3::read();
		Cr3::write(mmu_context.cr3);
		Cr3::Tlb_ignore::set(cr3, 1);
		Cr3::write(cr3);
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


Kernel::Sys_reg_access_result Cpu::user_msr_read(addr_t const msr,
                                                 addr_t &value)
{
	using namespace Kernel;

	uint32_t msr_addr = msr & 0xffffffff;

	unsigned const family = cpuid_1.family();
	unsigned const model  = cpuid_1.model();

	bool const nehalem_or_newer     = (family == 0x6);
	bool const sandybridge_or_newer = (family == 0x6) && (model >= 0x2a);
	bool const haswell_or_newer     = (family == 0x6) && (model >= 0x3c);
	bool const cannonlake           = (family == 0x6) && (model == 0x66);

	value = 0;

	switch(msr_addr) {
	case IA32_APERF:
	case IA32_MPERF:
		{
			if (!cpuid_6.mperf_aperf())
				return Sys_reg_access_result::FAILED;
			break;
		}
	case IA32_THERM_STATUS:
		{
			if (!cpuid_1.acpi())
				return Sys_reg_access_result::FAILED;
			break;
		}
	case IA32_PACKAGE_THERM_STATUS:
		{
			if (!cpuid_6.pkg_therm_mgmt())
				return Sys_reg_access_result::FAILED;
			break;
		}
	case IA32_PM_ENABLE:
	case IA32_HWP_CAPABILITIES:
	case IA32_HWP_REQUEST:
		{
			if (!cpuid_6.hwp())
				return Sys_reg_access_result::FAILED;
			break;
		}
	case IA32_HWP_REQUEST_PKG:
		{
			if (!cpuid_6.hwp_request_pkg())
				return Sys_reg_access_result::FAILED;
			break;
		}
	case IA32_ENERGY_PERF_BIAS:
		{
			if (!cpuid_6.energy_perf_bias())
				return Sys_reg_access_result::FAILED;
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
		return Sys_reg_access_result::FAILED;
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
		return Sys_reg_access_result::FAILED;
	case MSR_DRAM_ENERGY_STATUS:
	case MSR_DRAM_PERF_STATUS:
	case MSR_PKG_PERF_STATUS:
	case MSR_PKG_C8_RESIDENCY:
	case MSR_PKG_C9_RESIDENCY:
	case MSR_PKG_C10_RESIDENCY:
		if (haswell_or_newer)
			break;
		return Sys_reg_access_result::FAILED;
	case MSR_CORE_C1_RESIDENCY:
		if (cannonlake)
			break;
		return Sys_reg_access_result::FAILED;
	default:
		return Sys_reg_access_result::FAILED;
	};

	uint32_t low, high;
	asm volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr_addr));
	value = ((uint64_t)high << 32) | (low & ~0U);
	return Sys_reg_access_result::OK;
}


Kernel::Sys_reg_access_result Cpu::user_msr_write(addr_t const msr,
                                                  addr_t const value)
{
	using namespace Kernel;

	uint32_t msr_addr = msr & 0xffffffff;

	switch(msr_addr) {
	case IA32_PM_ENABLE:
	case IA32_HWP_REQUEST:
		{
			if (!cpuid_6.hwp())
				return Sys_reg_access_result::FAILED;
			break;
		}
	case IA32_HWP_REQUEST_PKG:
		{
			if (!cpuid_6.hwp_request_pkg())
				return Sys_reg_access_result::FAILED;
			break;
		}
	case IA32_ENERGY_PERF_BIAS:
		{
			if (!cpuid_6.energy_perf_bias())
				return Sys_reg_access_result::FAILED;
			break;
		}
	default:
		return Sys_reg_access_result::FAILED;
	};

	asm volatile ("wrmsr" :: "a" (value), "d" (value>>32), "c" (msr_addr));
	return Sys_reg_access_result::OK;
}


bool Cpu::rear(Cpu const &other) const
{
	if (cpuid_1a.atom_core_type() && !other.cpuid_1a.atom_core_type())
		return true;

	if (!cpuid_1a.atom_core_type() && other.cpuid_1a.atom_core_type())
		return false;

	return cpuid_1.apic_id() > other.cpuid_1.apic_id();
}


void Cpu::print(Output &output) const
{
	Genode::print(output, "Cpu apic_id=", Hex(cpuid_1.apic_id()), " [",
	              topology[5], ":", topology[4], ":", topology[3], ":",
	              topology[2], ":", topology[1], ":", topology[0], "] ",
	              cpuid_1a.atom_core_type() ? "ATOM " : "");
}


Cpu::Cpu()
:
	_id(Cpu::executing_id())
{
	/*
	 * First determine topology of cpu
	 */
	size_t shift = 0;
	auto level = [&] (unsigned i, auto &cpuid)
	{
		if (!cpuid.valid() ||
		    (cpuid.domain_type() == Domain_type::INVALID))
			return false;

		size_t bits = cpuid.shift_count() - shift;
		topology[i] = (cpuid.x2apic_id() >> shift) & ((1 << bits) - 1);
		shift = cpuid.shift_count();
		return true;
	};

	for (unsigned i = 0; i < TOPOLOGY_MAX; i++) {
		Cpuid_1f cpuid_1f(cpuid_0.max_leaf(), i);
		if (level(i, cpuid_1f))
			continue;

		Cpuid_b cpuid_b(cpuid_0.max_leaf(), i);
		if (!level(i, cpuid_b))
			break;
	}

	if (!cpuid_1.xsave() || !cpuid_d_0.valid())
		return;

	xstate_support = Xstate_support::XSAVE;

	Cpu::Cr4::access_t cr4 = Cpu::Cr4::read();
	Cpu::Cr4::Osxsave::set(cr4, 1);
	Cpu::Cr4::write(cr4);

	if (cpuid_d_1.xsaveopt())
		xstate_support = Xstate_support::XSAVEOPT;

	if (cpuid_d_1.xsaves()) {
		xstate_support = Xstate_support::XSAVES;

		/* we don't make use of the extended supervisor state save/restore */
		Cpu::Ia32_xss::write(0);
	}

	static uint64_t hw_supported =
		Cpu::Xstate_components::X87::bits(1) |
		Cpu::Xstate_components::Sse::bits(1) |
		Cpu::Xstate_components::Avx::bits(1) |
		Cpu::Xstate_components::Avx_512::bits(0b111);
	xcr0 = cpuid_d_0.xcr0() & hw_supported;
	Cpu::Xcr0::write(xcr0);

	/* we have to re-read CPUID 0DH after setting XCR0 */
	cpuid_d_0.read();
	if (cpuid_d_0.xsave_bytes_enabled() > Cpu::Fpu_context::SIZE)
		error("XSAVE state is bigger than kernel's specified size!");
}
