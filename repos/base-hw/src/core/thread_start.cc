/*
 * \brief  Implementation of Thread API interface for core
 * \author Stefan Kalkowski
 * \author Martin Stein
 * \date   2014-02-27
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/thread.h>
#include <base/sleep.h>
#include <base/env.h>

/* base-internal stack */
#include <base/internal/stack.h>
#include <base/internal/native_utcb.h>

/* core includes */
#include <map_local.h>
#include <platform.h>
#include <platform_thread.h>
#include <trace/source_registry.h>

using namespace Core;

namespace Hw { extern Untyped_capability _main_thread_cap; }


Thread::Start_result Thread::start()
{
	return _stack.convert<Start_result>([&] (Stack &stack) {

		Native_thread &nt = stack.native_thread();

		/* start thread with stack pointer at the top of stack */
		if (!nt.platform_thread)
			return Start_result::DENIED;;

		auto &pt = *nt.platform_thread;
		pt.start((void *)&_thread_start, (void *)stack.top());
		pt.create_trace_source(Core::Trace::sources(), *this);
		return Start_result::OK;

	}, [&] (Stack_error) { return Start_result::DENIED; });
}


void Thread::_deinit_native_thread(Stack &stack)
{
	destroy(platform().core_mem_alloc(), stack.native_thread().platform_thread);
}


void Thread::_init_native_thread(Stack &stack)
{
	stack.native_thread().platform_thread = new (platform().core_mem_alloc())
		Core_platform_thread(name, stack.utcb(), _affinity);
}


void Thread::_init_native_main_thread(Stack &stack)
{
	/* remap initial main-thread UTCB according to stack-area spec */
	map_local(Core::Platform::core_main_thread_phys_utcb(),
	          (addr_t)&stack.utcb(),
	          max(sizeof(Native_utcb) / PAGE_SIZE, (size_t)1));

	/* adjust initial object state in case of a main thread */
	stack.native_thread().cap = Hw::_main_thread_cap;
}
