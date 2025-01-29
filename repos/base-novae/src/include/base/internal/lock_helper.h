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


static inline bool thread_check_stopped_and_restart(Genode::Thread *thread_base)
{
	auto sem = thread_base
	         ? thread_base->native_thread().exc_pt_sel + Novae::SM_SEL_EC
	         : main_thread_running_semaphore();

	Novae::sm_ctrl(sem, Novae::SEMAPHORE_UP);

	return true;
}


static inline void thread_switch_to(Genode::Thread *) { }


static inline void thread_stop_myself(Genode::Thread *myself)
{
	auto sem = myself ? myself->native_thread().exc_pt_sel + Novae::SM_SEL_EC
	                  : main_thread_running_semaphore();

	if (Novae::NOVA_OK != Novae::sm_ctrl(sem, Novae::SEMAPHORE_DOWNZERO))
		nova_die();
}


static inline void thread_yield() { }
