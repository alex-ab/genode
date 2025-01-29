/*
 * \brief  NOVA-specific implementation of the Thread API for core
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \author Alexander Boettcher
 * \date   2010-01-19
 */

/*
 * Copyright (C) 2010-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/thread.h>

/* base-internal includes */
#include <base/internal/stack.h>

/* NOVAe includes */
#include <novae/syscalls.h>

/* core includes */
#include <platform.h>
#include <novae_util.h>
#include <trace/source_registry.h>
#include <pager.h>


using namespace Core;


void Thread::_init_platform_thread(size_t, Type type)
{
	/*
	 * This function is called for constructing server activations and pager
	 * objects. It allocates capability selectors for the thread's execution
	 * context and a synchronization-helper semaphore needed for 'Lock'.
	 */
	using namespace Novae;

	if (type == MAIN)
	{
		/* set EC selector according to NOVA spec */
		native_thread().ec_sel = platform_specific().core_pd_sel() + 1;

		/*
		 * Exception base of first thread in core is 0. We have to set
		 * it here so that Thread code finds the semaphore of the
		 * main thread.
		 */
		native_thread().exc_pt_sel = 0;

		return;
	}
	native_thread().ec_sel     = cap_map().insert(2);
	native_thread().exc_pt_sel = cap_map().insert(NUM_INITIAL_PT_LOG2);

	/* create running semaphore required for locking */
	addr_t rs_sel = native_thread().exc_pt_sel + SM_SEL_EC;
	uint8_t res = create_sm(rs_sel, platform_specific().core_pd_sel(), 0);
	if (res != NOVA_OK)
		error("Thread::_init_platform_thread: create_sm returned ", res);
}


void Thread::_deinit_platform_thread()
{
	auto const core_pd = platform_specific().core_obj_sel();

	revoke(core_pd, Novae::Obj_crd(native_thread().ec_sel, 2));
	revoke(core_pd, Novae::Obj_crd(native_thread().exc_pt_sel, Novae::NUM_INITIAL_PT_LOG2));

	for (unsigned i = 0; i < (1 << 2); i ++)
		Pager_object::untrack_rpc_cap(native_thread().ec_sel + i);

	/* potentially already done in pager.cc in cleanup_call */
	for (unsigned i = 0; i < Novae::NUM_INITIAL_PT; i ++)
		Pager_object::untrack_rpc_cap(native_thread().exc_pt_sel + i);

	cap_map().remove(native_thread().ec_sel, 2);
	cap_map().remove(native_thread().exc_pt_sel, Novae::NUM_INITIAL_PT_LOG2);

	/* revoke UTCB XXX - not support by NOVAe */
	addr_t utcb = reinterpret_cast<addr_t>(&_stack->utcb());
	revoke(platform_specific().core_host_sel(),
	       Novae::Mem_crd(utcb >> 12, 0, Novae::Rights::none()));
}


Thread::Start_result Thread::start()
{
	using namespace Novae;

	/* create local EC */
	auto res = create_ec(native_thread().ec_sel,
	                     platform_specific().core_pd_sel(),
	                     platform_specific().kernel_cpu_id(_affinity),
	                     reinterpret_cast<addr_t>(&_stack->utcb()),
	                     _stack->top(),
	                     native_thread().exc_pt_sel,
	                     false);
	if (res != NOVA_OK) {
		error("Thread::start: create_ec returned ", res);
		return Start_result::DENIED;
	}

	res = map_pagefault_portal(0, native_thread().exc_pt_sel,
	                           platform_specific().core_obj_sel(),
	                           platform_specific().core_obj_sel());

	if (res != NOVA_OK) {
		error("Thread::start: failed to create page-fault portal");
		return Start_result::DENIED;
	}

	Pager_object::enable_delegation(native_thread().exc_pt_sel,
	                                name() != "pager");

	return Start_result::OK;
}
