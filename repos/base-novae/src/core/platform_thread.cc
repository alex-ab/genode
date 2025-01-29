/*
 * \brief  Thread facility
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \author Alexander Boettcher
 * \date   2009-10-02
 */

/*
 * Copyright (C) 2009-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <ipc_pager.h>
#include <platform.h>
#include <platform_thread.h>
#include <platform_pd.h>
#include <util.h>
#include <novae_util.h>

/* base-internal includes */
#include <base/internal/stack_area.h>

/* NOVAe includes */
#include <novae/capability_space.h>
#include <novae/syscalls.h>
#include <novae/util.h>

using namespace Core;


static uint8_t map_thread_portals(Pager_object & pager,
                                  auto const dst_exc_base, auto const ec_sel)
{
	using namespace Novae;

	auto const source_pd    = platform_specific().core_obj_sel();
	auto const target_pd    = pager.pd_sel_obj();
	auto const src_exc_base = pager.exc_pt_sel_core();

	auto const grant = [&](auto const offset, auto const log2, auto rights, uint64_t src_abs = 0ul) {
		Obj_crd src_caps(src_abs ? : src_exc_base + offset, log2, rights);
		Obj_crd dst_caps(dst_exc_base + offset, log2, rights);
		return async_map(source_pd, target_pd, src_caps, dst_caps);
	};

	auto const pt_rights = Obj_crd::RIGHT_PT_CALL | Obj_crd::RIGHT_PT_EVENT;
	auto const sm_rights = Obj_crd::RIGHT_SM_UP   | Obj_crd::RIGHT_SM_DOWN;
	auto const ec_rights = Obj_crd::RIGHT_EC_RECALL;

	auto                       res = grant(0, NUM_PT_ARCH_LOG2, pt_rights);
	if (res == Novae::NOVA_OK) res = grant(PT_SEL_STARTUP,  0, pt_rights);
	if (res == Novae::NOVA_OK) res = grant(PT_SEL_RECALL,   0, pt_rights);
	if (res == Novae::NOVA_OK) res = grant(PT_SEL_DELEGATE, 0, pt_rights);
	if (res == Novae::NOVA_OK) res = grant(SM_SEL_EC      , 0, sm_rights);
	if (res == Novae::NOVA_OK) res = grant(EC_SEL_THREAD  , 0, ec_rights, ec_sel);

//	pager.track_delegation(src_exc_base, dst_exc_base, EC_SEL_THREAD /* count */);
	pager.track_delegation(ec_sel, dst_exc_base + EC_SEL_THREAD, 1);

	return res;
}


/*********************
 ** Platform thread **
 *********************/


void Platform_thread::affinity(Affinity::Location)
{
	if (!_pager)
		return;

	if (worker() || vcpu() || !sc_created())
		return;

	error(__func__, " - migration not supported");
}


void Platform_thread::start(void *ip, void *sp)
{
	using namespace Novae;

	if (!_pager) {
		error("pager undefined");
		return;
	}

	Pager_object &pager = *_pager;

	if (main_thread() && !vcpu() && (_pd.parent_pt_sel() == Native_thread::INVALID_INDEX)) {
		error("protection domain undefined");
		return;
	}

	auto const kernel_cpu_id = platform_specific().kernel_cpu_id(_location);
	auto const core_obj_sel  = platform_specific().core_obj_sel();

	if (vcpu()) {
		error("vcpu creation missing -> utcb addr becomes vapic pointer !!! -> use create_vcpu");
		return;
	}

	if (!main_thread()) {
		addr_t const initial_sp = reinterpret_cast<addr_t>(sp);
		addr_t const utcb_addr  = vcpu() ? 0 : round_page(initial_sp);

		if (_sel_exc_base == Native_thread::INVALID_INDEX) {
			error("exception base not specified");
			return;
		}

		uint8_t res = create_ec(_sel_ec(), _pd.pd_sel(), kernel_cpu_id,
				                utcb_addr, initial_sp, _sel_exc_base,
				                !worker());

		if (res != Novae::NOVA_OK) {
			error("creation of new thread failed ", res);
			return;
		}

		if (worker()) {
			/* local/worker threads do not require a startup portal */
			revoke(core_obj_sel, Obj_crd(pager.exc_pt_sel_core() + PT_SEL_STARTUP, 0));
		}

		if (!vcpu())
			res = map_thread_portals(pager, _sel_exc_base, _sel_ec());

		if (res != NOVA_OK) {
			revoke(core_obj_sel, Obj_crd(_sel_ec(), 0));
			error("creation of new thread/vcpu failed ", res);
			return;
		}

		pager.initial_register((addr_t)ip, (addr_t)sp);
		pager.track_selectors(_sel_ec(), _sel_exc_base);

		return;
	}

	if (!vcpu() && _sel_exc_base != Native_thread::INVALID_INDEX) {
		error("thread already started");
		return;
	}

	addr_t pd_utcb = 0;

	if (!vcpu()) {
		_sel_exc_base = 0;

		pd_utcb = stack_area_virtual_base() + stack_virtual_size() - get_page_size();

		addr_t remap_src[] = { _pd.parent_pt_sel() };
		addr_t remap_dst[] = { PT_SEL_PARENT };

		auto const source_pd = platform_specific().core_obj_sel();

		/* remap exception portals for first thread */
		for (auto i = 0u; i < sizeof(remap_dst) / sizeof(remap_dst[0]); i++) {

			if (async_map(source_pd, pager.pd_sel_obj(),
			              Obj_crd(remap_src[i], 0),
			              Obj_crd(remap_dst[i], 0))) {
				error("thread creation ", name(), " failed");
				return;
			}

			pager.track_delegation(remap_src[i], remap_dst[i], 1);
		}
	}

	if (vcpu()) {
		error("vcpu creation missing -> utcb addr becomes vapic pointer !!! -> use create_vcpu");
		return;
	}

	/* create first thread in task */
	enum { THREAD_GLOBAL = true };
	uint8_t res = create_ec(_sel_ec(), _pd.pd_sel(), kernel_cpu_id,
	                        pd_utcb, 0, _sel_exc_base,
	                        THREAD_GLOBAL);
	if (res != NOVA_OK) {
		error("create_ec returned ", res);
		return;
	}

	pager.track_selectors(_sel_ec(), _sel_exc_base);
	pager.initial_register((addr_t)ip, (addr_t)sp);

	if (vcpu())
		_features |= REMOTE_PD;
	else
		res = map_thread_portals(pager, 0, _sel_ec());

	if (res == NOVA_OK) {
		/* let the thread run */
		res = create_sc(_sel_sc(), _pd.pd_sel(), _sel_ec(),
			            Qpd(Qpd::DEFAULT_QUANTUM, _priority));
	}

	if (res != NOVA_OK) {
		pager.track_selectors(Native_thread::INVALID_INDEX,
		                      Native_thread::INVALID_INDEX);
		pager.initial_register(0, 0);

		error("create_sc returned ", res);

		/* cap_selector free for _sel_ec is done in de-constructor */
		revoke(core_obj_sel, Obj_crd(_sel_ec(), 0));
		return;
	}

	_features |= SC_CREATED;
}


void Platform_thread::pause()
{
	if (!_pager)
		return;

	_pager->client_recall(true);
}


void Platform_thread::resume()
{
	using namespace Novae;

	if (worker() || sc_created()) {
		if (_pager)
			_pager->wake_up();
		return;
	}

	if (!_pager) {
		error("pager undefined - resuming thread failed");
		return;
	}

	uint8_t res = create_sc(_sel_sc(), _pd.pd_sel(), _sel_ec(),
			                Qpd(Qpd::DEFAULT_QUANTUM, _priority));

	if (res == NOVA_OK)
		_features |= SC_CREATED;
	else
		error("create_sc failed ", res);
}


Thread_state Platform_thread::state()
{
	Thread_state s { };
	if (_pager && _pager->copy_thread_state(&s))
		return s;

	return { .state = Thread_state::State::UNAVAILABLE, .cpu = { } };
}


void Platform_thread::state(Thread_state s)
{
	if (_pager && _pager->copy_thread_state(s))
		/* the new state is transferred to the kernel by the recall handler */
		_pager->client_recall(false);
}


void Platform_thread::single_step(bool on)
{
	if (!_pager) return;

	_pager->single_step(on);
}


const char * Platform_thread::pd_name() const { return _pd.name(); }


Trace::Execution_time Platform_thread::execution_time() const
{
	uint64_t sc_time = 0;

	if (!sc_created())
		return { 0, sc_time, Novae::Qpd::DEFAULT_QUANTUM, _priority };

	uint8_t res = Novae::sc_ctrl(_sel_sc(), sc_time);
	if (res != Novae::NOVA_OK)
		warning("sc_ctrl failed res=", res);

	return { 0, sc_time, Novae::Qpd::DEFAULT_QUANTUM, _priority };
}


void Platform_thread::pager(Pager_object &pager)
{
	_pager = &pager;
	_pager->assign_pd(_pd.pd_sel());
}


void Platform_thread::thread_type(Cpu_session::Native_cpu::Thread_type thread_type,
                                  Cpu_session::Native_cpu::Exception_base exception_base)
{
	/* you can do it only once */
	if (_sel_exc_base != Native_thread::INVALID_INDEX)
		return;

	if (!main_thread() || (thread_type == Cpu_session::Native_cpu::Thread_type::VCPU))
		_sel_exc_base = exception_base.exception_base;

	if (thread_type == Cpu_session::Native_cpu::Thread_type::LOCAL)
		_features |= WORKER;
	else if (thread_type == Cpu_session::Native_cpu::Thread_type::VCPU)
		_features |= VCPU;
}


Platform_thread::Platform_thread(Platform_pd &pd, size_t, const char *name,
                                 unsigned prio, Affinity::Location affinity, addr_t)
:
	_pd(pd), _pager(0), _id_base(cap_map().insert(1)),
	_sel_exc_base(Native_thread::INVALID_INDEX),
	_location(platform_specific().sanitize(affinity)),
	_features(0),
	_priority((uint8_t)(scale_priority(prio, name, _pd.name()))),
	_name(name)
{
	if (!pd.has_any_threads)
		_features |= MAIN_THREAD;

	pd.has_any_threads = true;
}


Platform_thread::~Platform_thread()
{
	if (_pager) {
		/* reset pager and badge used for debug output */
		_pager->reset_badge();
		_pager = 0;
	}

	auto const core_obj_sel = platform_specific().core_obj_sel();

	/* free ec and sc caps */
	revoke(core_obj_sel, Novae::Obj_crd(_id_base, 1));
	cap_map().remove(_id_base, 1);
}
