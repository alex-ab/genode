/*
 * \brief  Implementation of IRQ session component
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \author Alexander Boettcher
 * \date   2009-10-02
 */

/*
 * Copyright (C) 2009-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <irq_root.h>
#include <irq_args.h>
#include <platform.h>

/* NOVAe includes */
#include <novae/syscalls.h>
#include <novae_util.h>
#include <trace/source_registry.h>

using namespace Core;


static bool irq_ctrl(addr_t const irq_sel, addr_t const irq_idx,
                     addr_t &msi_addr, addr_t &msi_data,
                     Novae::Gsi_flags const flags, addr_t const sbdf)
{
	/* assign IRQ to CPU && request msi data to be used by driver */
	uint8_t res = Novae::assign_int(irq_sel, flags.value(),
	                                kernel_hip().cpu_bsp(), irq_idx, sbdf,
	                                msi_addr, msi_data);

	if (res != Novae::NOVA_OK)
		error("setting up GSI/MSI failed - error ", res);

	/* nova syscall interface specifies msi addr/data to be 32bit */
	msi_addr = msi_addr & ~0U;
	msi_data = msi_data & ~0U;

	return res == Novae::NOVA_OK;
}


static bool associate_gsi(addr_t irq_sel, addr_t irq_idx, addr_t bdf,
                          Novae::Gsi_flags gsi_flags)
{
	addr_t dummy1 = 0, dummy2 = 0;

	return irq_ctrl(irq_sel, irq_idx, dummy1, dummy2, gsi_flags, bdf);
}


static bool associate_msi(addr_t irq_sel, addr_t irq_idx, addr_t bdf,
                          addr_t &msi_addr, addr_t &msi_data)
{
	return irq_ctrl(irq_sel, irq_idx, msi_addr, msi_data, { }, bdf);
}


void Irq_object::sigh(Signal_context_capability cap)
{
	if (!_sigh_cap.valid() && !cap.valid())
		return;

	if (_sigh_cap.valid() && _sigh_cap == cap) {
		/* avoid useless overhead, e.g. with IOMMUs enabled */
		return;
	}

	if (_sigh_cap.valid() && !cap.valid()) {
		_sigh_cap = Signal_context_capability();
		return;
	}

	/* associate GSI or MSI to device */
	bool ok = false;
	if (_irq_type == Irq_session::TYPE_LEGACY)
		ok = associate_gsi(irq_sel(), _idx, _sbdf, _gsi_flags);
	else
		ok = associate_msi(irq_sel(), _idx, _sbdf, _msi_addr, _msi_data);

	if (!ok) {
		_sigh_cap = Signal_context_capability();
		return;
	}

	_sigh_cap = cap;
}


void Irq_object::ack_irq()
{
	_wait_for_ack.wakeup();
}


Thread::Start_result Irq_object::start(unsigned irq, addr_t const bdf, Irq_args const &irq_args)
{
	if (Novae::create_sm_irq(irq_sel(), platform_specific().core_pd_sel(), irq) != Novae::NOVA_OK)
		return Start_result::DENIED;

	/* initialize GSI IRQ flags */
	auto gsi_flags = [] (Irq_args const &args) {
		if (args.trigger() == Irq_session::TRIGGER_UNCHANGED
		 || args.polarity() == Irq_session::POLARITY_UNCHANGED)
			return Novae::Gsi_flags();

		if (args.trigger() == Irq_session::TRIGGER_EDGE)
			return Novae::Gsi_flags(Novae::Gsi_flags::EDGE);

		if (args.polarity() == Irq_session::POLARITY_HIGH)
			return Novae::Gsi_flags(Novae::Gsi_flags::HIGH);
		else
			return Novae::Gsi_flags(Novae::Gsi_flags::LOW);
	};

	_gsi_flags = gsi_flags(irq_args);
	_irq_type  = irq_args.type();
	_sbdf      = bdf;
	_idx       = irq;

	/* associate GSI or MSI (and retrieve _msi_addr and _msi_data) to device */
	bool ok = false;
	if (_irq_type == Irq_session::TYPE_LEGACY)
		ok = associate_gsi(irq_sel(), _idx, _sbdf, _gsi_flags);
	else
		ok = associate_msi(irq_sel(), _idx, _sbdf, _msi_addr, _msi_data);

	if (!ok)
		return Start_result::DENIED;

	/* Thread::start() replacement */
	return start();
}


Irq_object::Irq_object()
:
	Thread(Weight::DEFAULT_WEIGHT, "core_irq", 4096 /* stack */, Type::NORMAL),
	_kernel_caps(cap_map().insert(0))
{ }


Irq_object::~Irq_object()
{
	auto const core_pd = platform_specific().core_obj_sel();
	revoke(core_pd, Novae::Obj_crd(_kernel_caps, 0));

	cap_map().remove(_kernel_caps, 0);
}


/***************************
 ** IRQ session component **
 ***************************/


Irq_session_component::Irq_session_component(Range_allocator &irqs,
                                             const char      *args)
:
	_irq_number(~0U), _irq_alloc(irqs), _irq_object()
{
	Irq_args const irq_args(args);

	auto const irq = unsigned(irq_args.irq_number());
	auto const bdf = Arg_string::find_arg(args, "bdf").long_value(0x10000u);

	if (irq_args.type() == Irq_session::TYPE_LEGACY) {
		if (irq >= kernel_hip().gsi_pin()) {
			error("GSI out of range ", irq, ">", kernel_hip().gsi_pin());
			throw Service_denied();
		}

		if (irqs.alloc_addr(1, irq).failed()) {
			error("unavailable GSI ", irq, " requested");
			throw Service_denied();
		}

		_irq_number = irq;

		error("GSI ", _irq_number, " ", Genode::Hex(bdf));

	} else {

		auto result = irqs.alloc_aligned(1, 0,
		                                 { .start = kernel_hip().gsi_pin(),
		                                   .end   = kernel_hip().gsi_max() + 1 });

		if (result.failed()) {
			error("Out of MSIs");
			throw Service_denied();
		}

		result.with_result([&](auto value) { _irq_number = unsigned(addr_t(value)); },
		                   [ ](auto){});

		Genode::error("MSI ", irq, "->", _irq_number, " bdf=", Hex(bdf));
	}

	if (Thread::Start_result::OK != _irq_object.start(_irq_number, bdf, irq_args))
		throw Service_denied();
}


Irq_session_component::~Irq_session_component()
{
	if (_irq_number == ~0U)
		return;

	addr_t free_irq = _irq_number;
	_irq_alloc.free((void *)free_irq);
}


void Irq_session_component::ack_irq()
{
	_irq_object.ack_irq();
}


void Irq_session_component::sigh(Signal_context_capability cap)
{
	_irq_object.sigh(cap);
}


void Irq_object::entry()
{
	/* thread is up and ready */
	while (true) {

		_wait_for_ack.block();

		auto res = Novae::sm_ctrl(irq_sel(), Novae::SEMAPHORE_DOWN);
		if (res != Novae::NOVA_OK)
			error(this, " wait for IRQ failed ", res);

		if (_sigh_cap.valid())
			Signal_transmitter(_sigh_cap).submit(1);
	}
}


Irq_session::Info Irq_session_component::info()
{
	if (!_irq_object.msi_address() || !_irq_object.msi_value())
		return { .type = Info::Type::INVALID, .address = 0, .value = 0 };

	return {
		.type    = Info::Type::MSI,
		.address = _irq_object.msi_address(),
		.value   = _irq_object.msi_value()
	};
}


static Genode::Blockade & wait_for_irq_construction()
{
	static Genode::Blockade blockade { };
	return blockade;
}


static void global_irq_thread_entry()
{
	auto & myself = *Thread::myself();

	wait_for_irq_construction().wakeup();

	try {
		myself.entry();
	} catch (...) {
		try {
			raw("Thread '", myself.name(),
			    "' died because of an uncaught exception");
		} catch (...) {
			/* die in a noisy way */
			*(unsigned long *)0 = 0xdead;
		}
		throw;
	}
}


Genode::Thread::Start_result Irq_object::start()
{
	using namespace Novae;

	auto res = create_ec(native_thread().ec_sel,
	                     platform_specific().core_pd_sel(),
	                     platform_specific().kernel_cpu_id(_affinity),
	                     reinterpret_cast<addr_t>(&_stack->utcb()),
	                     _stack->top(),
	                     native_thread().exc_pt_sel,
	                     true);

	if (res != NOVA_OK) {
		error("Thread::start: create_ec returned ", res);
		return Start_result::DENIED;
	}

	res = map_exception_portals(0, native_thread().exc_pt_sel,
	                            platform_specific().core_obj_sel(),
	                            platform_specific().core_obj_sel());

	if (res != NOVA_OK)
		error("Thread::start: failed to create exceptions portals");

	Pager_object::enable_delegation(native_thread().exc_pt_sel,
	                                name() != "pager");

	/* set infos during startup portal traversal, see platform.cc */
	Utcb & new_utcb = *reinterpret_cast<Utcb *>(&_stack->utcb());
	new_utcb.ip(addr_t(global_irq_thread_entry));
	new_utcb.sp(_stack->top());

	/* set information for startup portal for global IRQ threads in core */
	res = pt_ctrl(PT_SEL_STARTUP, addr_t(&new_utcb),
	              Mtd(Mtd::GPR_0_7 | Mtd::EIP).value());

	if (res != Novae::NOVA_OK) {
		error(__func__, ":", __LINE__, " returned ", res);
		return Start_result::DENIED;
	}

	res = async_map(platform_specific().core_obj_sel(),
	                platform_specific().core_obj_sel(),
	                Obj_crd(PT_SEL_STARTUP, 0),
	                Obj_crd(native_thread().exc_pt_sel + PT_SEL_STARTUP, 0));

	if (res != NOVA_OK) {
		error("Thread::start: failed to setup startup portal");
		return Start_result::DENIED;
	}

	/* let the thread run */
	res = create_sc(native_thread().ec_sel + 2,
	                platform_specific().core_pd_sel(),
	                native_thread().ec_sel,
	                Qpd(Qpd::DEFAULT_QUANTUM, Qpd::DEFAULT_PRIORITY));
	if (res != NOVA_OK) {
		error("Thread::start: failed to create SC");
		return Start_result::DENIED;
	}

	wait_for_irq_construction().block();

	struct Core_trace_source : public  Core::Trace::Source::Info_accessor,
	                           private Core::Trace::Control,
	                           private Core::Trace::Source
	{
		Thread & thread;

		/**
		 * Trace::Source::Info_accessor interface
		 */
		Info trace_source_info() const override
		{
			uint64_t sc_time = 0;

			uint8_t res = Novae::sc_ctrl(thread.native_thread().ec_sel + 2, sc_time);
			if (res != Novae::NOVA_OK)
				warning("sc_time for core thread failed res=", res);

			return { Session_label("core"), thread.name(),
			         Trace::Execution_time(0, sc_time), thread.affinity() };
		}

		Core_trace_source(Core::Trace::Source_registry &registry, Thread &t)
		:
			Core::Trace::Control(),
			Core::Trace::Source(*this, *this), thread(t)
		{
			registry.insert(this);
		}
	};

	new (platform().core_mem_alloc())
		Core_trace_source(Core::Trace::sources(), *this);

	return Start_result::OK;
}
