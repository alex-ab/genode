/*
 * \brief  Helper functions for the Lock implementation
 * \author Alexander Boettcher
 * \date   2024-06-19
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* Genode includes */
#include <base/thread.h>
#include <base/stdint.h>

/* base-internal includes */
#include <base/internal/native_thread.h>

#include <novae/syscalls.h>
#include <novae/util.h>

extern int main_thread_running_semaphore();


static inline Genode::addr_t sm_sel_ec(Genode::Thread *thread_ptr)
{
	if (!thread_ptr)
		return main_thread_running_semaphore();

	using namespace Genode;

	return thread_ptr->with_native_thread(
		[&] (Native_thread &nt) { return nt.exc_pt_sel + Novae::SM_SEL_EC; },
		[&] { error("attempt to synchronize invalid thread"); return 0UL; });
}


static inline bool thread_check_stopped_and_restart(Genode::Thread *thread_ptr)
{
	Novae::sm_ctrl(sm_sel_ec(thread_ptr), Novae::SEMAPHORE_UP);
	return true;
}


static inline void thread_switch_to(Genode::Thread *) { }


static inline void thread_stop_myself(Genode::Thread *myself)
{
	Novae::sm_ctrl(sm_sel_ec(myself), Novae::SEMAPHORE_DOWNZERO);
}


static inline void thread_yield() { }
