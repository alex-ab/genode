/*
 * \brief  Class for kernel data that is needed to manage a specific CPU
 * \author Reto Buerki
 * \author Stefan Kalkowski
 * \date   2015-02-09
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <kernel/cpu.h>
#include <hw/memory_consts.h>


void Kernel::Cpu::_arch_init()
{
	gdt.init((addr_t)&tss);
	Idt::init();
	Tss::init();

	/* set interrupt stack pointer to kernel context stack minus FPU state size */
	tss.ist[0] = stack_start();

	_pic.init();
	_timer.init();
	_ipi_irq.init();

	/* resume tsc value, if it got saved during suspend */
	if (tsc) Cpu::Ia32_tsc_value::write(tsc);

	/* enable timer interrupt */
	_pic.unmask(_timer.interrupt_id(), id());

	/* Initialize TSC AUX with individual value per cpu */
	Cpu::Ia32_tsc_aux::write((Cpu::Ia32_tsc_aux::access_t)_id.value);

	if (xstate_support == Xstate_support::LEGACY)
		return;

	Cpu::Cr4::access_t cr4 = Cpu::Cr4::read();
	Cpu::Cr4::Osxsave::set(cr4, 1);
	Cpu::Cr4::write(cr4);

	/* we don't make use of the extended supervisor state save/restore */
	if (xstate_support == Xstate_support::XSAVES)
		Cpu::Ia32_xss::write(0);

	Cpu::Xcr0::write(xcr0);
}


[[noreturn]] void Kernel::Cpu::panic(Genode::Cpu_state &state)
{
	using namespace Genode;
	using Cs = Genode::Cpu_state;

	const char *reason = "unknown";

	switch (state.trapno) {
	case Cs::PAGE_FAULT:            reason = "page-fault";            break;
	case Cs::UNDEFINED_INSTRUCTION: reason = "undefined instruction"; break;
	case Cs::SUPERVISOR_CALL:       reason = "system-call";           break;
	default:
		if (state.trapno >= Cs::INTERRUPTS_START &&
		    state.trapno <= Cs::INTERRUPTS_END)
			reason = "interrupt";
	};

	log("");
	log("Kernel panic on CPU ", Cpu::executing_id());
	log("Exception reason is ", reason, " (trapno=", state.trapno, ")");
	log("");
	log("Register dump:");
	log("ip     = ", Hex(state.ip));
	log("sp     = ", Hex(state.sp));
	log("cs     = ", Hex(state.cs));
	log("ss     = ", Hex(state.ss));
	log("eflags = ", Hex(state.eflags));
	log("rax    = ", Hex(state.rax));
	log("rbx    = ", Hex(state.rbx));
	log("rcx    = ", Hex(state.rcx));
	log("rdx    = ", Hex(state.rdx));
	log("rdi    = ", Hex(state.rdi));
	log("rsi    = ", Hex(state.rsi));
	log("rbp    = ", Hex(state.rbp));
	log("CR2    = ", Hex(Cpu::Cr2::read()));
	log("CR3    = ", Hex(Cpu::Cr3::read()));

	while (true) asm volatile("hlt");
}
