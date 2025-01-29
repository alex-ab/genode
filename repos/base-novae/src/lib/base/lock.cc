/*
 * \brief  Lock implementation
 * \author Alexander Boettcher
 * \author Norman Feske
 * \date   2024-11-24
 */

/*
 * Copyright (C) 2009-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/lock.h>

/* base-internal includes */
#include <base/internal/spin_lock.h>

using namespace Genode;


static inline Genode::Thread *invalid_thread_base()
{
	return (Genode::Thread*)~0UL;
}


static inline bool thread_base_valid(Genode::Thread *thread_base)
{
	return (thread_base != invalid_thread_base());
}


/********************
 ** Lock applicant **
 ********************/

void Lock::Applicant::wake_up()
{
	if (!thread_base_valid(_thread_base)) return;

	/*
	 * Deal with the race that may occur in the 'lock' function between
	 * releasing the spinlock and blocking myself.
	 */

	for (;;) {

		if (thread_check_stopped_and_restart(_thread_base))
			return;

		thread_switch_to(_thread_base);
	}
}

#ifdef __x86_64__
inline int cmpxchgl(volatile addr_t *dest, addr_t cmp_val, addr_t new_val)
{
	addr_t tmp;

	__asm__ __volatile__
	(
	 "lock cmpxchgq %1, %3 \n\t"
	 :
	 "=a" (tmp)      /* 0 EAX, return val */
	 :
	 "r"  (new_val), /* 1 reg, new value */
	 "0"  (cmp_val), /* 2 EAX, compare value */
	 "m"  (*dest)    /* 3 mem, destination operand */
	 :
	 "memory", "cc"
	);

	return tmp == cmp_val;
}
#else
inline int cmpxchgl(volatile addr_t *dest, addr_t cmp_val, addr_t new_val)
{
	addr_t fail, old_val = 0;

	do {
		asm volatile(
			"ldxr %w1, %2      \n"
			"mov  %w0, #0      \n"
			"cmp  %w1, %w3     \n"
			"b.ne 1f           \n"
			"stxr %w0, %w4, %2 \n"
			"1:                \n"
			: "=&r" (fail), "=&r" (old_val), "+Q" (*dest)
			:  "r" (cmp_val), "r" (new_val)
			: "cc");
	} while (fail);

	Genode::memory_barrier();
	return (old_val != cmp_val) ? 0 : 1;
}
#endif


/***************
 ** Lock lock **
 ***************/

void Lock::lock()
{
	Applicant myself(Thread::myself());
	lock(myself);
}


void Lock::lock(Applicant &myself)
{
	/* replace temporary "myself" on stack with _owner in _last_applicant */
	auto replace_myself_with_owner = [&]() {
		if (cmpxchgl((addr_t *)&_last_applicant, addr_t(&myself), addr_t(&_owner)))
			return true; /* got the lock */

		/*
		 * _last_applicant changed, need to search myself in list,
		 * safe to do so, since the lock owner is myself
		 */

		Applicant * iter = _last_applicant;
		Applicant * next { };

		while (iter && *iter != myself) {
			next = iter;
			iter = iter->applicant_to_wake_up();
		}

		/* should not happen */
		if (!iter)
			return false;

		/* got myself, replace with _owner object, myself is on stack */
		_owner.applicant_to_wake_up(iter->applicant_to_wake_up());
		if (next)
			next->applicant_to_wake_up(&_owner);

		return true; /* got the lock */
	};


	/* re-try loop, if cmpxchg fails due to concurrent access */
	while (true) {

		Applicant * const prev = _last_applicant;

		/* XXX double lock() lock() is not detected by this ... */
		bool const blockade_usage = prev && *prev == myself;

		if (blockade_usage)
			myself.applicant_to_wake_up(prev->applicant_to_wake_up());
		else
			myself.applicant_to_wake_up(prev);

		/* using on stack "myself" temporarily to get position in _last_applicant */
		if (!cmpxchgl((addr_t *)&_last_applicant, addr_t(prev), addr_t(&myself)))
			continue;

		if (blockade_usage) {
			_owner = myself;

			replace_myself_with_owner();
			/* stop thread */
		}

		if (!prev) {
			_owner = myself;
			_state = LOCKED;

			if (replace_myself_with_owner())
				return; /* got lock */

			/* should not happen case */
		}

		/* stop thread */
		break;
	}

	thread_stop_myself(myself.thread_base());
}


void Lock::unlock()
{
	/* replace temporary "myself" on stack with _owner in _last_applicant */
	auto replace_iter_with_owner = [&](Applicant * expect) {
		if (cmpxchgl((addr_t *)&_last_applicant, addr_t(expect), addr_t(&_owner)))
			return true; /* got the lock */

		/*
		 * _last_applicant changed, need to search expect in list,
		 * safe to do so, since the lock owner is expect
		 */

		Applicant * iter = _last_applicant;
		Applicant * next { };

		while (iter && *iter != *expect) {
			next = iter;
			iter = iter->applicant_to_wake_up();
		}

		/* should not happen */
		if (!iter)
			return false;

		/* got myself, replace with _owner object, myself is on stack */
		_owner.applicant_to_wake_up(iter->applicant_to_wake_up());
		if (next)
			next->applicant_to_wake_up(&_owner);

		return true; /* got the lock */
	};

	Applicant myself(Thread::myself());

	while (true) {
		Applicant * iter { _last_applicant };
		Applicant * next { };

		while (iter && iter->applicant_to_wake_up()) {
			next  = iter;
			iter  = iter->applicant_to_wake_up();
		}

		/* no new owner */
		if (!iter)
			return;

		/* only one element in _last_applicant case */
		if (!next) {

			/* blockade use case - what about multiple concurrent unblocks () ? XXX */
			if (*iter != myself) {
				_owner = *iter;

				replace_iter_with_owner(iter);

				iter->wake_up();
				return;
			}

			auto backup_owner = _owner;

			_state = UNLOCKED;
			_owner = Applicant(invalid_thread_base());

			if (cmpxchgl((addr_t *)&_last_applicant, addr_t(iter), 0ul))
				return; /* success */

			/* retry on concurrent access */
			_state = LOCKED;
			_owner = backup_owner;
			continue;
		}

		/* replace next with reference to _owner to avoid stack issues */
		next->applicant_to_wake_up(nullptr);

		_owner = *next;

		if (_last_applicant == next) {
			if (cmpxchgl((addr_t *)&_last_applicant, addr_t(next), addr_t(&_owner))) {
				_owner.wake_up();
				return;
			}
		}

		iter = _last_applicant;
		next = { };

		while (iter && *iter != _owner) {
			next = iter;
			iter = iter->applicant_to_wake_up();
		}

		if (iter && next)
			next->applicant_to_wake_up(&_owner);

		_owner.wake_up();

		return;
	}
}


Lock::Lock(Lock::State initial)
:
	_spinlock_state(SPINLOCK_UNLOCKED),
	_state(UNLOCKED),
	_last_applicant(0),
	_owner(invalid_thread_base())
{
	if (initial == LOCKED)
		lock();
}
