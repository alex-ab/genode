/*
 * \brief   Kernel backend for execution contexts in userland
 * \author  Martin Stein
 * \author  Stefan Kalkowski
 * \date    2013-11-11
 */

/*
 * Copyright (C) 2013-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* base includes */
#include <cpu/memory_barrier.h>

/* base-hw core includes */
#include <kernel/cpu.h>
#include <kernel/pd.h>
#include <kernel/thread.h>

using namespace Kernel;

extern "C" void kernel_to_user_context_switch(Board::Cpu::Fpu_context*,
                                              Board::Cpu::Context*, void*);


void Thread::Syscall_arguments::_register(unsigned idx, Call_arg arg)
{
	if (idx == 0) _state.r0 = arg;
}


Call_arg Thread::Syscall_arguments::_register(unsigned idx) const
{
	switch (idx) {
		case 0: return _state.r0;
		case 1: return _state.r1;
		case 2: return _state.r2;
		case 3: return _state.r3;
		case 4: return _state.r4;
		case 5: return _state.r5;
		default: ;
	};
	return 0;
}


void Thread::Syscall_arguments::write(Kernel::time_t const t)
{
	/* split 64-bit time_t value into 2 register */
	_state.r0 = (addr_t) (t >> 32UL);
	_state.r1 = t & ~0UL;
}


Cpu_suspend_result Core_thread::_call_cpu_suspend(unsigned const) {
	return Cpu_suspend_result::FAILED; }


void Thread::exception(Genode::Cpu_state &state)
{
	using Ctx = Board::Cpu::Context;

	switch (state.cpu_exception) {
	case Ctx::SUPERVISOR_CALL:
		_call(state);
		return;
	case Ctx::PREFETCH_ABORT:
	case Ctx::DATA_ABORT:
		_mmu_exception(state);
		return;
	case Ctx::INTERRUPT_REQUEST:
	case Ctx::FAST_INTERRUPT_REQUEST:
		_interrupt();
		return;
	case Ctx::UNDEFINED_INSTRUCTION:
		_die("Undefined instruction at ip=", Genode::Hex(state.ip));
		return;
	case Ctx::RESET:
		return;
	default:
		_die("Unknown exception triggered: ", state.cpu_exception);
		return;
	}
}


/**
 * on ARM with multiprocessing extensions, maintainance operations on TLB,
 * and caches typically work coherently across CPUs when using the correct
 * coprocessor registers (there might be ARM SoCs where this is not valid,
 * with several shareability domains, but until now we do not support them)
 */
void Kernel::Core_thread::Tlb_invalidation::execute(Cpu &) { }


void Core_thread::Flush_and_stop_cpu::execute(Cpu &) { }


void Cpu::Halt_job::load() { }


void Thread::save(Cpu_state &state) { _save(state); }


void Thread::load(Cpu_state &state)
{
	auto context = static_cast<Board::Cpu::Context*>(&state);
	kernel_to_user_context_switch((static_cast<Board::Cpu::Fpu_context*>(context)),
	                              context, (void*)_cpu().stack_start());
}


void Thread::load()
{
	if (!_cpu().active(_pd.mmu_regs) && !_privileged())
		_cpu().switch_to(_pd.mmu_regs);

	load(*regs);
}
