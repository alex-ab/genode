/*
 * \brief  Kernel backend for x86 virtual machines
 * \author Benjamin Lamowski
 * \date   2022-10-14
 */

/*
 * Copyright (C) 2022-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */
#include <base/log.h>
#include <util/construct_at.h>
#include <util/mmio.h>
#include <cpu/string.h>

#include <map_local.h>
#include <platform.h>
#include <kernel/cpu.h>
#include <kernel/vcpu.h>
#include <kernel/main.h>

#include <svm.h>
#include <vmx.h>

using namespace Genode;

using Kernel::Cpu;
using Kernel::Vcpu;


Vcpu::Vcpu(Cpu                    &cpu,
           Board::Vcpu_state      &state,
           Kernel::Signal_context &context,
           Identity               &id)
:
	Kernel::Object { *this },
	Cpu_context(cpu, Scheduler::Group_id::BACKGROUND),
	_state(state),
	_context(context),
	_id(id),
	_vcpu_context(id.id, state, cpu) { }


Vcpu::~Vcpu() { }


void Vcpu::run()
{
	if (_cpu().id() != Cpu::executing_id()) {
		error("vCPU run called from remote core.");
		return;
	}

	/*
	 * On first start, initialize the vCPU
	 */
	if (_vcpu_context.init_state == Board::Vcpu_context::Init_state::CREATED) {
		_vcpu_context.initialize(_cpu(),
		    reinterpret_cast<addr_t>(_id.table));
		_vcpu_context.tsc_aux_guest = _cpu().id().value;
		_vcpu_context.init_state  = Board::Vcpu_context::Init_state::STARTED;
	}

	_state.with_state([&] (Genode::Vcpu_state &state) {
		_vcpu_context.load(state); });

	if (_scheduled != ACTIVE) Cpu_context::_activate();
	_scheduled = ACTIVE;
}


void Vcpu::pause()
{
	if (_cpu().id() != Cpu::executing_id()) {
		Genode::error("vCPU pause called from remote core.");
		return;
	}

	/*
	 * The vCPU isn't initialized yet when the VMM first queries the state.
	 * Just return so that the VMM gets presented with the default startup
	 * exit code set at construction.
	 */
	if (_vcpu_context.init_state != Board::Vcpu_context::Init_state::STARTED)
		return;

	_pause_vcpu();

	_state.with_state([&] (Genode::Vcpu_state &state) {
		_vcpu_context.store(state); });

	/*
	 * Set exit code so that if _run() was not called after an exit, the
	 * next exit due to a signal will be interpreted as PAUSE request.
	 */
	_vcpu_context.exit_reason = Board::EXIT_PAUSED;
}


void Vcpu::load(Cpu_state &state)
{
	_vcpu_context.virt.switch_world(state, _cpu().stack_start());
}


void Vcpu::load()
{
	Cpu::Ia32_tsc_aux::write(
	    (Cpu::Ia32_tsc_aux::access_t)_vcpu_context.tsc_aux_guest);

	bool const xsave_avail = _vcpu_context.regs->fpu_context().xstate_support
	                         != Cpu::Xstate_support::LEGACY;

	_state.with_state([&] (auto &state) {
		if (state.fpu.charged())
			state.fpu.with_state([&](auto const &fpu) {
				memcpy(&_vcpu_context.regs->fpu_context(), &fpu, 512); });
	});
	_vcpu_context.regs->fpu_context().load();

	if (xsave_avail && _vcpu_context.xcr0 != _cpu().xcr0) {
		/*
		 * Sanity check delivered xcr0 value to only use
		 * xstates supported by hw kernel, (and thereby cleaned),
		 * and to fulfill hardware restrictions that otherwise lead
		 * to hardware exceptions
		 */
		auto v = _vcpu_context.xcr0 & _cpu().xcr0;
		Cpu::Xstate_components::X87::set(v, 1);
		if (Cpu::Xstate_components::Avx_512::get(v))
			Cpu::Xstate_components::Avx::set(v, 1);
		if (Cpu::Xstate_components::Avx::get(v))
			Cpu::Xstate_components::Sse::set(v, 1);
		Cpu::Xcr0::write(v);
		_vcpu_context.xcr0 = v;
	}

	load(*_vcpu_context.regs);
}


void Vcpu::save(Cpu_state &state)
{
	Genode::memcpy(&*_vcpu_context.regs, &state, sizeof(Cpu_state));

	bool const xsave_avail = _vcpu_context.regs->fpu_context().xstate_support
	                         != Cpu::Xstate_support::LEGACY;

	if (xsave_avail && _vcpu_context.xcr0 != _cpu().xcr0)
		Cpu::Xcr0::write(_cpu().xcr0);

	_vcpu_context.regs->fpu_context().save();
	_state.with_state([&] (auto &state) {
		state.fpu.charge([&](auto &fpu) {
			memcpy(&fpu, &_vcpu_context.regs->fpu_context(), 512);
			return 512;
		});
	});

	Cpu::Ia32_tsc_aux::write((Cpu::Ia32_tsc_aux::access_t)_cpu().id().value);
}


void Vcpu::exception(Cpu_state &state)
{
	using namespace Board;

	bool pause = false;

	switch (state.trapno) {
		case TRAP_VMEXIT:
			_vcpu_context.exit_reason =
				_vcpu_context.virt.handle_vm_exit();
			/*
			 * If handle_vm_exit() returns EXIT_PAUSED, the vCPU has
			 * exited due to a host interrupt. The exit reason is
			 * set to EXIT_PAUSED so that if the VMM queries the
			 * vCPU state while the vCPU is stopped, it is clear
			 * that it does not need to handle a synchronous vCPU exit.
			 *
			 * VMX jumps directly to __kernel_entry when exiting
			 * guest mode and skips the interrupt vectors, therefore
			 * trapno will not be set to the host interrupt and we
			 * have to explicitly handle interrupts here.
			 *
			 * SVM on the other hand will service the host interrupt
			 * after the stgi instruction (see
			 * AMD64 Architecture Programmer's Manual Vol 2
			 * 15.17 Global Interrupt Flag, STGI and CLGI Instructions)
			 * and will jump to the interrupt vector, setting trapno
			 * to the host interrupt. This means the exception
			 * handler should actually skip this case branch, which
			 * is fine because _vcpu_context.exit_reason is set to
			 * EXIT_PAUSED by default, so a VMM querying the vCPU
			 * state will still get the right value.
			 *
			 * For any other exit reason, we exclude this vCPU
			 * thread from being scheduled and signal the VMM that
			 * it needs to handle an exit.
			 */
			if (_vcpu_context.exit_reason == EXIT_PAUSED)
				_interrupt();
			else
				pause = true;
			break;
		case Cpu_state::INTERRUPTS_START ... Cpu_state::INTERRUPTS_END:
			_interrupt();
			break;
		case TRAP_VMDEAD:
			_pause_vcpu();
			break;
		default:
			error("Vcpu: triggered unknown exception ",
			              state.trapno,
			              " with error code ", state.errcode,
			              " at ip=",
			              (void *)state.ip, " sp=", (void *)state.sp);
			_pause_vcpu();
			break;
	};

	if (pause == true) {
		_pause_vcpu();
		_context.submit(1);
	}
}


Board::Virt_interface&
Board::Vcpu_context::detect_virtualization(Vcpu_state &state, Id &id)
{
	/*
	 * we can safely strip id result to one byte,
	 * the id allocator has a max of 256
	 */
	uint8_t vmid = 0;
	id.with_result([&] (auto const &i) { vmid = (uint8_t)i; },
	               [&] (auto&) {
		/*
		 * The following should not occur, because id creation result
		 * is checked during vm session creation, only hint here in
		 * case of development regression
		 */
		Genode::error("No virtualization id available!");
		vmid = 255;
	});

	struct Virt_support
	{
		bool svm { false };
		bool vmx { false };

		Virt_support()
		{
			Core::Platform::apply_with_boot_info([&](auto const &boot_info) {
				svm = boot_info.plat_info.has_svm;
				vmx = boot_info.plat_info.has_vmx;
			});
		}
	};

	static Virt_support const vsupport {};

	if (vsupport.svm)
		return *Genode::construct_at<Vmcb>((void*)state.vmc_addr(),
		                                   state, vmid);
	if (vsupport.vmx)
		return *Genode::construct_at<Vmcs>((void*)state.vmc_addr(), state);

	Genode::error( "No virtualization support detected.");
	static Virt_interface dummy(state);
	return dummy;
}


Board::Vcpu_context::Vcpu_context(Id id, Board::Vcpu_state &state, Cpu &cpu)
:
	regs(true, cpu),
	virt(detect_virtualization(state, id))
{
	regs->trapno = TRAP_VMEXIT;
}


void Board::Vcpu_context::load(Genode::Vcpu_state &state)
{
	virt.load(state);

	if (state.cx.charged() || state.dx.charged() || state.bx.charged()) {
		regs->rax   = state.ax.value();
		regs->rcx   = state.cx.value();
		regs->rdx   = state.dx.value();
		regs->rbx   = state.bx.value();
	}

	if (state.bp.charged() || state.di.charged() || state.si.charged()) {
		regs->rdi   = state.di.value();
		regs->rsi   = state.si.value();
		regs->rbp   = state.bp.value();
	}

	if (state.r8 .charged() || state.r9 .charged() ||
	    state.r10.charged() || state.r11.charged() ||
	    state.r12.charged() || state.r13.charged() ||
	    state.r14.charged() || state.r15.charged()) {

		regs->r8  = state.r8.value();
		regs->r9  = state.r9.value();
		regs->r10 = state.r10.value();
		regs->r11 = state.r11.value();
		regs->r12 = state.r12.value();
		regs->r13 = state.r13.value();
		regs->r14 = state.r14.value();
		regs->r15 = state.r15.value();
	}

	if (state.tsc_aux.charged())
		tsc_aux_guest = state.tsc_aux.value();

	if (state.xcr0.charged()) xcr0 = state.xcr0.value();
}


void Board::Vcpu_context::store(Genode::Vcpu_state &state)
{
	state.discharge();
	state.exit_reason = (unsigned) exit_reason;

	/* SVM will overwrite rax but VMX doesn't. */
	state.ax.charge(regs->rax);
	state.cx.charge(regs->rcx);
	state.dx.charge(regs->rdx);
	state.bx.charge(regs->rbx);

	state.di.charge(regs->rdi);
	state.si.charge(regs->rsi);
	state.bp.charge(regs->rbp);

	state.r8.charge(regs->r8);
	state.r9.charge(regs->r9);
	state.r10.charge(regs->r10);
	state.r11.charge(regs->r11);
	state.r12.charge(regs->r12);
	state.r13.charge(regs->r13);
	state.r14.charge(regs->r14);
	state.r15.charge(regs->r15);

	state.tsc.charge(Hw::X86_64_cpu::rdtsc());
	state.tsc_aux.charge(tsc_aux_guest);

	state.xcr0.charge(xcr0);
	state.xss.charge(xss);

	virt.store(state);
}


void Board::Vcpu_context::initialize(Board::Cpu &cpu, addr_t table_phys_addr)
{
	virt.initialize(cpu, table_phys_addr);
}
