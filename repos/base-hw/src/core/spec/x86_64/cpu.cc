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
