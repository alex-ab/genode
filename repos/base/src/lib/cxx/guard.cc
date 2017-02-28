/*
 * \brief  Support for guarded variables
 * \author Christian Helmuth
 * \date   2009-11-18
 */

/*
 * Copyright (C) 2009-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/log.h>
#include <base/semaphore.h>
#include <cpu/atomic.h>


static Genode::Semaphore sem(0);

namespace __cxxabiv1 
{

	/*
	 * A guarded variable can be in three states:
	 *
	 *   1) not initialized
	 *   2) in initialization (transient)
	 *   3) initialized
	 *
	 * The generic ABI uses the first byte of a 64-bit guard variable for state
	 * 1), 2) and 3). ARM-EABI uses the first byte of a 32-bit guard variable.
	 * Therefore we define '__guard' as a 32-bit type and use the least
	 * significant byte for 1) and 3) and the following byte for 2) and let the
	 * other threads spin until the guard is released by the thread in
	 * initialization.
	 */

	typedef int __guard;

	enum State { INIT_NONE = 0, INIT_DONE = 1, IN_INIT = 0x100 };

	extern "C" int __cxa_guard_acquire(__guard *guard)
	{
		volatile char *initialized = (char *)guard;
		volatile int  *in_init     = (int  *)guard;

		int prev_state;
		do {
			/* check for state 3) */
			if (*initialized) return 0;

			/* atomically set new state and increase blockers counter */
			prev_state = *in_init & ~INIT_DONE;
		} while (!Genode::cmpxchg(in_init, prev_state, prev_state + IN_INIT));

		/* check for state 2) */
		if (prev_state == INIT_NONE) {
			/*
			 * The guard was acquired. The caller is allowed to initialize the
			 * guarded variable and required to call __cxa_guard_release() to flag
			 * initialization completion (and unblock other guard applicants).
			 */
			return 1;
		}

		/* failed to acquire the guard */

		/* wait until state 3) is reached while other thread is in init */
		do {
			/* block thread - FIFO is important here  */
			sem.down();

			/* if the wakeup was not for this thread, wake next one */
			if (*initialized == INIT_NONE)
				sem.up();
		} while (*initialized == INIT_NONE);

		do {
			/* decrease blocker count */
			prev_state = *in_init;
		} while (!Genode::cmpxchg(in_init, prev_state, prev_state - IN_INIT));

		/* if other threads block for this guard, wake next one */
		if ((prev_state - IN_INIT) > (IN_INIT | INIT_DONE))
			sem.up();

		/* guard not acquired */
		return 0;
	}


	extern "C" void __cxa_guard_release(__guard *guard)
	{
		volatile int *in_init = (int *)guard;

		/* set state 3) */
		if (Genode::cmpxchg(in_init, IN_INIT, IN_INIT | INIT_DONE))
			return;

		/* we have others waiting for us, wake one thread up */
		while (!Genode::cmpxchg(in_init, *in_init, *in_init | INIT_DONE)) { }

		sem.up();
	}


	extern "C" void __cxa_guard_abort(__guard *) {
		Genode::error(__func__, " called"); }
}
