/*
 * \brief  Core-specific instance of the VM session interface
 * \author Alexander Boettcher
 * \author Christian Helmuth
 * \date   2018-08-26
 */

/*
 * Copyright (C) 2018-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/cache.h>
//#include <cpu/vcpu_state.h>
#include <util/list.h>
#include <util/flex_iterator.h>

/* core includes */
#include <cpu_thread_component.h>
#include <dataspace_component.h>
#include <vm_session_component.h>
#include <platform.h>
#include <pager.h>
#include <util.h>

/* NOVAe includes */
#include <novae_util.h>
#include <novae/cap_map.h>
#include <novae/syscalls.h>

using namespace Core;


enum { CAP_RANGE_LOG2 = 2, CAP_RANGE = 1 << CAP_RANGE_LOG2 };

static addr_t invalid_sel() { return ~0UL; }

static Novae::uint8_t map_async_caps(Novae::Obj_crd const /* src */,
                                    Novae::Obj_crd const /* dst */,
                                    addr_t const /* dst_pd */)
{
#if 0
	using Novae::Utcb;

//	Utcb &utcb = *reinterpret_cast<Utcb *>(Thread::myself()->utcb());
	addr_t const src_pd = platform_specific().core_pd_sel();

	utcb.set_msg_word(0);
	/* ignore return value as one item always fits into the utcb */
	bool const ok = utcb.append_item(src, 0);
	(void)ok;
#endif

	error(__func__, " not implemented");

	/* asynchronously map capabilities */
	return Novae::NOVA_TIMEOUT;
//	return Novae::delegate(src_pd, dst_pd, dst);
}


static uint8_t _with_kernel_quota_upgrade(addr_t const, auto const &fn)
{
	uint8_t res;

	res = fn();

	return res;
}


/********************************
 ** Vm_session_component::Vcpu **
 ********************************/

Core::Trace::Source::Info Vm_session_component::Vcpu::trace_source_info() const
{
	uint64_t sc_time = 0;

	uint8_t res = Novae::sc_ctrl(sc_sel(), sc_time);
	if (res != Novae::NOVA_OK)
		warning("vCPU sc_ec_time failed res=", res);

	return { _label, String<5>("vCPU"),
	         Trace::Execution_time(0, sc_time,
	                               Novae::Qpd::DEFAULT_QUANTUM, _priority),
	         _location };
}


void Vm_session_component::Vcpu::startup()
{
	/* initialize SC on first call - do nothing on subsequent calls */
	if (_alive) return;

	uint8_t res = _with_kernel_quota_upgrade(_pd.value, [&] {
		return Novae::create_sc(sc_sel(), _pd.value, ec_sel(),
		                       Novae::Qpd(Novae::Qpd::DEFAULT_QUANTUM, _priority));
	});

	if (res == Novae::NOVA_OK)
		_alive = true;
	else
		error("create_sc=", res);
}


void Vm_session_component::Vcpu::exit_handler(unsigned const exit,
                                              Signal_context_capability const cap)
{
	if (!cap.valid())
		return;

	if (exit >= Novae::NUM_INITIAL_VCPU_PT)
		return;

	/* map handler into vCPU-specific range of VM protection domain */
	addr_t const pt = Novae::NUM_INITIAL_VCPU_PT * _id + exit;

	uint8_t res = _with_kernel_quota_upgrade(_pd.value, [&] {
		Novae::Obj_crd const src(cap.local_name(), 0);
		Novae::Obj_crd const dst(pt, 0);

		return map_async_caps(src, dst, _pd.value);
	});

	if (res != Novae::NOVA_OK)
		error("map pt ", res, " failed");
}


Vm_session_component::Vcpu::Vcpu(Rpc_entrypoint            &ep,
                                 Accounted_ram_allocator   &ram_alloc,
                                 Cap_quota_guard           &cap_alloc,
                                 unsigned const             id,
                                 unsigned const             kernel_id,
                                 Affinity::Location const   location,
                                 unsigned const             priority,
                                 Session_label const       &label,
                                 Pd_selector               &pd_sel,
                                 addr_t const               core_pd_sel,
                                 addr_t const               vmm_pd_sel,
                                 Trace::Control_area       &trace_control_area,
                                 Trace::Source_registry    &trace_sources)
:
	_ep(ep),
	_ram_alloc(ram_alloc),
	_cap_alloc(cap_alloc),
	_trace_sources(trace_sources),
	_sel_sm_ec_sc(invalid_sel()),
	_id(id),
	_location(location),
	_priority(priority),
	_label(label),
	_pd(pd_sel),
	_trace_control_slot(trace_control_area.alloc())
{
	using namespace Novae;

	/* account caps required to setup vCPU */
	Cap_quota_guard::Result caps = _cap_alloc.reserve(Cap_quota{CAP_RANGE});
	if (caps.failed()) {
		constructed = Alloc_error::OUT_OF_CAPS;
		return;
	}

	/* now try to allocate cap indexes */
	_sel_sm_ec_sc = cap_map().insert(CAP_RANGE_LOG2);
	if (_sel_sm_ec_sc == invalid_sel()) {
		error("out of caps in core");
		return;
	}

	/* setup resources */
	uint8_t res = _with_kernel_quota_upgrade(_pd.value, [&] {
		return Novae::create_sm(sm_sel(), core_pd_sel, 0);
	});

	if (res != Novae::NOVA_OK) {
		cap_map().remove(_sel_sm_ec_sc, CAP_RANGE_LOG2);
		error("create_sm = ", res);
		return;
	}

	error("vcpu creation missing -> utcb addr becomes vapic pointer !!! -> use create_vcpu");
	return;

	addr_t const event_base = (1U << Novae::NUM_INITIAL_VCPU_PT_LOG2) * id;
	enum { TIME_OFFSETTING = false, NO_UTCB = 0, NO_STACK = 0 };
	res = _with_kernel_quota_upgrade(_pd.value, [&] {
		return Novae::create_vcpu(ec_sel(), _pd.value, kernel_id,
		                          NO_UTCB, NO_STACK, event_base, TIME_OFFSETTING);
	});

	if (res != Novae::NOVA_OK) {
		cap_map().remove(_sel_sm_ec_sc, CAP_RANGE_LOG2);
		error("create_ec = ", res);
		return;
	}

	addr_t const dst_sm_ec_sel = Novae::NUM_INITIAL_PT_RESERVED + _id*CAP_RANGE;

	res = _with_kernel_quota_upgrade(vmm_pd_sel, [&] {
		enum { CAP_LOG2_COUNT = 1 };
		int permission = Obj_crd::RIGHT_EC_RECALL | Obj_crd::RIGHT_SM_UP |
		                 Obj_crd::RIGHT_SM_DOWN;
		Obj_crd const src(sm_sel(), CAP_LOG2_COUNT, permission);
		Obj_crd const dst(dst_sm_ec_sel, CAP_LOG2_COUNT);

		return map_async_caps(src, dst, vmm_pd_sel);
	});

	if (res != Novae::NOVA_OK) {
		cap_map().remove(_sel_sm_ec_sc, CAP_RANGE_LOG2);
		error("map sm ", res, " ", _id);
		return;
	}

	_ep.manage(this);

	trace_control_area.with_control(_trace_control_slot,
		[&] (Trace::Control &control) {
			_trace_source.construct(*this, control);
			Trace::Source &source = *_trace_source;
			_trace_sources.insert(&source);
		});

	caps.with_result([&] (Cap_quota_guard::Reservation &r) { r.deallocate = false; },
	                 [&] (Cap_quota_guard::Error) { /* handled at 'reserve' */ });

	constructed = Ok();
}


Vm_session_component::Vcpu::~Vcpu()
{
	_ep.dissolve(this);

	if (_trace_source.constructed()) {
		Trace::Source &source = *_trace_source;
		_trace_sources.remove(&source);
	}

	if (_sel_sm_ec_sc != invalid_sel()) {
		_cap_alloc.replenish(Cap_quota{CAP_RANGE});
		cap_map().remove(_sel_sm_ec_sc, CAP_RANGE_LOG2);
	}
}

/**************************
 ** Vm_session_component **
 **************************/

bool Vm_session_component::Pd_selector::valid() const {
	return value && value != invalid_sel(); }


Vm_session_component::Pd_selector::Pd_selector()
: value(cap_map().insert()) {}


Vm_session_component::Pd_selector::~Pd_selector()
{
	if (valid()) cap_map().remove(value, 0);
}


Vm_session_component::Attach_result
Vm_session_component::attach(Dataspace_capability const,
                             addr_t const, Attach_attr)
{
	error(__func__, " not implemented ");
	return Region_map::Range(0, 0);
}


void Vm_session_component::_detach(addr_t guest_phys, size_t size)
{
	error(__func__, " not implemented ", Hex_range(guest_phys, size));
}


void Vm_session_component::detach(addr_t guest_phys, size_t size)
{
	_memory.detach(guest_phys, size, [&](addr_t vm_addr, size_t size) {
		_detach(vm_addr, size); });
}


void Vm_session_component::detach_at(addr_t const addr)
{
	_memory.detach_at(addr, [&](addr_t vm_addr, size_t size) {
		_detach(vm_addr, size); });
}


void Vm_session_component::reserve_and_flush(addr_t const addr)
{
	_memory.reserve_and_flush(addr, [&](addr_t vm_addr, size_t size) {
		_detach(vm_addr, size); });
}


Vm_session_component::Create_vcpu_result
Vm_session_component::create_vcpu(Thread_capability cap)
{
	if (!cap.valid()) return Create_vcpu_error::DENIED;

	/* lookup vmm pd and cpu location of handler thread in VMM */
	addr_t             kernel_cpu_id = 0;
	Affinity::Location vcpu_location;

	auto lambda = [&] (Cpu_thread_component *ptr) {
		if (!ptr)
			return invalid_sel();

		Cpu_thread_component &thread = *ptr;

		vcpu_location = thread.platform_thread().affinity();
		kernel_cpu_id = platform_specific().kernel_cpu_id(thread.platform_thread().affinity());

		return thread.platform_thread().pager().pd_sel();
	};
	addr_t const vmm_pd_sel = _ep.apply(cap, lambda);

	/* if VMM pd lookup failed then deny to create vCPU */
	if (!vmm_pd_sel || vmm_pd_sel == invalid_sel())
		return Create_vcpu_error::DENIED;

	/* XXX this is a quite limited ID allocator... */
	unsigned const vcpu_id = _next_vcpu_id;

	return _vcpu_alloc.create(_vcpus,
		                      _ep,
		                      _ram,
		                      _cap_quota_guard(),
		                      vcpu_id,
		                      (unsigned)kernel_cpu_id,
		                      vcpu_location,
		                      _priority,
		                      _session_label,
		                      _pd,
		                      platform_specific().core_pd_sel(),
		                      vmm_pd_sel,
		                      _trace_control_area,
		                      _trace_sources).template convert<Create_vcpu_result>(
		[&] (auto &a) {
			return a.obj.constructed.template convert<Create_vcpu_result>(
				[&] (auto) {
					a.deallocate = false;
					++_next_vcpu_id;
					return a.obj.cap(); },
				[&] (auto error) { return error; });
		},
		[] (auto err) { return err; });
}


Vm_session_component::Vm_session_component(Rpc_entrypoint &ep,
                                           Resources resources,
                                           Label const &label,
                                           Ram_allocator &ram,
                                           Local_rm &local_rm,
                                           unsigned const priority,
                                           Trace::Source_registry &trace_sources)
:
	Ram_quota_guard(resources.ram_quota),
	Cap_quota_guard(resources.cap_quota),
	_ep(ep),
	_trace_sources(trace_sources),
	_ram(ram, _ram_quota_guard(), _cap_quota_guard()),
	_trace_control_area(_ram, local_rm),
	_heap(_ram, local_rm),
	_memory(ep, *this, _ram, local_rm),
	_priority(scale_priority(priority, "VM session", label)),
	_session_label(label)
{
	if (!_cap_quota_guard().try_withdraw(Cap_quota{1})) {
		constructed = Session_error::OUT_OF_CAPS;
		return;
	}

	if (_trace_control_area.constructed.failed())
		constructed = _trace_control_area.constructed.convert<Session_error>(
			[] (auto &) { return Session_error::DENIED; /* never */ },
			[] (Alloc_error e) {
				switch(e) {
				case Alloc_error::OUT_OF_CAPS: return Session_error::OUT_OF_CAPS;
				case Alloc_error::OUT_OF_RAM:  return Session_error::OUT_OF_RAM;
				default: break;
				}
				return Session_error::DENIED;
			});

	if (!_pd.valid())
		return;

	addr_t const core_pd = platform_specific().core_pd_sel();

	uint8_t res = Novae::create_pd(_pd.value, core_pd, 3 /* guest space */);
	if (res != Novae::NOVA_OK) {
		error("create_pd = ", res);
		return;
	}

	constructed = Ok();
}


Vm_session_component::~Vm_session_component()
{
	_vcpus.for_each([&] (Vcpu &vcpu) {
		destroy(_heap, &vcpu); });
}
