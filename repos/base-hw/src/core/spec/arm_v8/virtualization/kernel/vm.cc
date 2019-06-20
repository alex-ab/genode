/*
 * \brief   Kernel backend for virtual machines
 * \author  Stefan Kalkowski
 * \date    2015-02-10
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/log.h>
#include <hw/assert.h>
#include <cpu/vm_state_virtualization.h>

#include <map_local.h>
#include <platform_pd.h>
#include <kernel/cpu.h>
#include <kernel/vm.h>

namespace Kernel
{
	/**
	 * ARM's virtual interrupt controller cpu interface
	 */
	struct Virtual_pic;

	/**
	 * ARM's virtual timer counter
	 */
	struct Virtual_timer;

	/**
	 * Kernel private virtualization interrupts, delivered to VM/VMMs
	 */
	struct Vm_irq;
}

using namespace Kernel;

extern "C" void   kernel();
extern     void * kernel_stack;
extern "C" void   hypervisor_enter_vm(Cpu::Context*);

struct Host_context {
	addr_t                    sp;
	addr_t                    ip;
	Cpu::Ttbr0_el1::access_t  ttbr0;
	Cpu::Ttbr1_el1::access_t  ttbr1;
	Cpu::Sctlr_el1::access_t  sctlr;
	Cpu::Tcr_el1::access_t    tcr;
	addr_t                    vm_state;
	Genode::uint64_t          vttbr_el2;
	Genode::uint64_t          esr_el2;
	Genode::uint64_t          elr_el1;
	Genode::uint64_t          sp_el0;

	Genode::uint64_t          mair_el1;
	Genode::uint64_t          vbar_el1;
	Genode::uint64_t          actlr_el1;
	Genode::uint64_t          amair_el1;
	Genode::uint64_t          par_el1;
	Genode::uint64_t          tpidrro_el0;
	Genode::uint64_t          tpidr_el0;
	Genode::uint64_t          tpidr_el1;

	Genode::uint32_t          spsel;
	Genode::uint32_t          spsr_el1;
	Genode::uint32_t          daif;
	Genode::uint32_t          nzcv;
	Genode::uint32_t          fpcr;
	Genode::uint32_t          fpsr;

	Genode::uint32_t          cpacr_el1;
	Genode::uint32_t          afsr0_el1;
	Genode::uint32_t          afsr1_el1;
	Genode::uint32_t          contextidr_el1;
	Genode::uint32_t          csselr_el1;
} vt_host_context;


struct Kernel::Vm_irq : Kernel::Irq
{
	Vm_irq(unsigned const irq)
	:
		Kernel::Irq(irq, cpu_pool().executing_cpu().irq_pool())
	{ }

	/**
	 * A VM interrupt gets injected into the VM scheduled on the current CPU
	 */
	void occurred() override
	{
	Genode::raw("--",__func__);
		Cpu_job & job = cpu_pool().executing_cpu().scheduled_job();
		Vm *vm = dynamic_cast<Vm*>(&job);
		if (!vm)
			Genode::raw("VM timer interrupt while VM is not runnning!");
		else
			vm->inject_irq(_irq_nr);
	}
};

using Vmid_allocator = Genode::Bit_allocator<256>;

static Vmid_allocator &alloc()
{
	static Vmid_allocator * allocator = nullptr;
	if (!allocator) {
		allocator = unmanaged_singleton<Vmid_allocator>();

		/* reserve VM ID 0 for the hypervisor */
		unsigned id = allocator->alloc();
		assert (id == 0);
	}
	return *allocator;
}

/* XXX */
static Genode::Vm_state _vm_state __attribute__((aligned(4096))) { };


Kernel::Vm::Vm(void                   * const state,
               Kernel::Signal_context * const context,
               void                   * const table)
:  Cpu_job(Cpu_priority::MIN, 0),
  _id(alloc().alloc()),
  _state((Genode::Vm_state *)state),
  _context(context),
  _table(table)
{
	affinity(cpu_pool().primary_cpu());
//	Virtual_pic::pic().irq.enable(); / * XXX */

	vt_host_context.ip    = (addr_t) &kernel;
	vt_host_context.sp    = _cpu->stack_start();
	vt_host_context.ttbr0 = Cpu::Ttbr0_el1::read();
	vt_host_context.ttbr1 = Cpu::Ttbr1_el1::read();
	vt_host_context.sctlr = Cpu::Sctlr_el1::read();
	vt_host_context.tcr   = Cpu::Tcr_el1::read();

	using namespace Genode;

	/* XXX map el2 stack low for hypervisor mode XXX */
	log( "v=", Hex(vt_host_context.sp - 0x1000),
	    " p=", Hex(Platform::core_phys_addr(vt_host_context.sp - 0x1000)),
	    " -> ", Hex(Hw::Mm::hypervisor_stack().base),
	    "+", Hex(Hw::Mm::hypervisor_stack().size),
	    " el2 stack");

	if (!map_local(Platform::core_phys_addr(vt_host_context.sp - 0x1000),
	               Hw::Mm::hypervisor_stack().base,
	               Hw::Mm::hypervisor_stack().size / 4096,
	               Hw::PAGE_FLAGS_KERN_DATA)) {
		error("mapping _state context failed");
		while (1) { }
	}
	/* assign el2 accessible low stack address */
	vt_host_context.sp = Hw::Mm::hypervisor_stack().base + 0xff0;


	/* XXX map static vm_state for hypervisor low XXX */
	log( "v=", Hex((addr_t)&_vm_state),
	    " p=", Hex(Platform::core_phys_addr((addr_t)&_vm_state)),
	    " -> ", Hex(Hw::Mm::hypervisor_vm_state().base), 
	    "+", Hex(Hw::Mm::hypervisor_vm_state().size),
	    " el2 vm_state");

	if (!Genode::map_local(Platform::core_phys_addr((addr_t)&_vm_state),
	                       Hw::Mm::hypervisor_vm_state().base,
	                       Hw::Mm::hypervisor_vm_state().size / 4096,
	                       Hw::PAGE_FLAGS_KERN_TEXT)) {
		Genode::error("mapping _state context failed");
		while (1) { }
	}

	vt_host_context.vm_state = Hw::Mm::hypervisor_vm_state().base;
}


Kernel::Vm::~Vm() { alloc().free(_id); }


void Kernel::Vm::exception(Cpu & /*cpu*/)
{
	/* XXX */
	memcpy(_state, &_vm_state, sizeof(_vm_state));

	Cpu::Esr::access_t esr = vt_host_context.esr_el2;

	Genode::raw("--", __func__, " reason=",
	            Genode::Hex(Cpu::Esr::Ec::get(esr)), " ",
	            Genode::Hex(Cpu::Esr::Iss::get(esr)),
	            " ip=", Genode::Hex(_state->ip));

	/* copy reason of exception to user state */
	_state->esr_el2 = esr;

	pause();
	_context->submit(1);

#if 0
	Virtual_timer::save(_state);

	switch(_state->cpu_exception) {
	case Genode::Cpu_state::INTERRUPT_REQUEST:
	case Genode::Cpu_state::FAST_INTERRUPT_REQUEST:
		_state->gic_irq = Board::VT_MAINTAINANCE_IRQ;
		_interrupt(cpu.id());
		break;
	default:
		pause();
		_context->submit(1);
	}

	Virtual_pic::save(_state);
	Virtual_timer::reset();
#endif
}


void Kernel::Vm::proceed(Cpu &)
{
	/*
	 * the following values have to be enforced by the hypervisor
	 */
	vt_host_context.vttbr_el2 = Cpu::Vttbr_el2::Ba::masked((Cpu::Vttbr_el2::access_t)_table);
	Cpu::Vttbr_el2::Asid::set(vt_host_context.vttbr_el2, _id);

	vt_host_context.esr_el2   = 0;
	vt_host_context.elr_el1   = Cpu::Elr_el1::read();
	vt_host_context.sp_el0    = Cpu::Sp_el0::read();
	vt_host_context.spsel     = Cpu::Spsel::read();
	vt_host_context.spsr_el1  = Cpu::Spsr_el1::read();
	vt_host_context.daif      = Cpu::Daif::read();
	vt_host_context.nzcv      = Cpu::Nzcv::read();
	vt_host_context.fpcr      = Cpu::Fpcr::read();
	vt_host_context.fpsr      = Cpu::Fpsr::read();
	vt_host_context.mair_el1  = Cpu::Mair::read();
	vt_host_context.vbar_el1  = Cpu::Vbar_el1::read();
	vt_host_context.actlr_el1 = Cpu::Actlr_el1::read();
	vt_host_context.amair_el1 = Cpu::Amair_el1::read();
	vt_host_context.par_el1   = Cpu::Par_el1::read();
	vt_host_context.tpidrro_el0 = Cpu::Tpidrro_el0::read();
	vt_host_context.tpidr_el0 = Cpu::Tpidr_el0::read();
	vt_host_context.tpidr_el1 = Cpu::Tpidr_el1::read();
	vt_host_context.cpacr_el1 = Cpu::Cpacr_el1::read();
	vt_host_context.afsr0_el1 = Cpu::Afsr0_el1::read();
	vt_host_context.afsr1_el1 = Cpu::Afsr1_el1::read();
	vt_host_context.contextidr_el1 = Cpu::Contextidr_el1::read();
	vt_host_context.csselr_el1 = Cpu::Csselr_el1::read();

	Genode::raw("--",__func__, " ", Genode::Hex(_state->ip));

	/* XXX */
	memcpy(&_vm_state, _state, sizeof(_vm_state));

#if 0
	Virtual_pic::load(_state);
	Virtual_timer::load(_state);

#endif

	hypervisor_enter_vm(reinterpret_cast<Cpu::Context*>(vt_host_context.vm_state));
}


void Vm::inject_irq(unsigned /*irq*/)
{ 
	Genode::raw("--",__func__);
#if 0
	_state->gic_irq = irq;
	pause();
	_context->submit(1);
#endif
}
