/*
 * \brief  Platform-specific helper functions for the _main() function
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \date   2009-12-28
 */

/*
 * Copyright (C) 2009-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _PLATFORM___MAIN_HELPER_H_
#define _PLATFORM___MAIN_HELPER_H_

#include <base/thread.h>
#include <base/env.h>

#include <base/cap_sel_alloc.h>
#include <base/native_types.h>

enum { STACK_SIZE_MAIN_THREAD = 16UL * sizeof(Genode::addr_t) * 1024 };

extern unsigned long _prog_img_data;
extern unsigned long _prog_img_end;

/**
 * Main thread
 *
 * It is created here to reserve the first context area for the main thread.
 */
class Main_thread : public Genode::Thread<STACK_SIZE_MAIN_THREAD> {

	public:

		explicit Main_thread() : Thread("main")
		{
			using namespace Genode;

			/* special handling for main thread in core */
			if (_tid.ec_sel != Native_thread::INVALID_INDEX) {
				/*
				 * Exception base of first thread in core is 0. We have to set
				 * it here so that Thread_base code finds the semaphore of the
				 * main thread.
				 */
				Nova::revoke(Nova::Obj_crd(_tid.exc_pt_sel,
				                           Nova::NUM_INITIAL_PT_LOG2), true);
				cap_selector_allocator()->free(_tid.exc_pt_sel,
				                               Nova::NUM_INITIAL_PT_LOG2);
				_tid.exc_pt_sel = 0;

				return;
			}

			/* remember pager cap of main thread */
			_pager_cap = reinterpret_cap_cast<Pager_object>(
			             Native_capability(Nova::PT_PAGER_MAIN));
		}

		void entry() {}
};

static void main_thread_bootstrap()
{
	using namespace Genode;

	/* get current stack pointer */
   	addr_t ptr_stack_current = 0;
	addr_t stack_current = reinterpret_cast<addr_t>(&ptr_stack_current);

	/* skip if current stack pointer isn't in range of data segment (ldso) */
	if (!((addr_t)&_prog_img_data <= stack_current &&
	      stack_current < (addr_t)&_prog_img_end))
		return;

	/* setup the current thread as first thread in the context area */
	static Main_thread main_thread;

	/* Get address of stack of main thread */
	addr_t stack_main = reinterpret_cast<addr_t>(main_thread.stack_top());

	/* If the current stack pointer is in range of main thread we're done */
	if ((stack_main - STACK_SIZE_MAIN_THREAD <= stack_current) &&
	    (stack_current < stack_main))
		return;

	/* The stack pointer doesn't match the one of main thread, that means
	 * we have to change it now. Call the assembly bootstrap code again with
	 * right stack pointer set in the eax register. The assembly code expects
	 * in eax the initial stack pointer.
	 */
	asm volatile ("jmp _start_restart;"
	              :
	              : "a" (stack_main)
	              : "memory");
}

#endif /* _PLATFORM___MAIN_HELPER_H_ */
