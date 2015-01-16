/*
 * \brief  Virtualbox adjusted pthread_create implementation
 * \author Alexander Boettcher
 * \date   2014-04-09
 */

/*
 * Copyright (C) 2014 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

/* Genode */
#include <base/printf.h>
#include <base/thread.h>
#include <base/env.h>
#include <cpu_session/connection.h>

/* Genode libc pthread binding */
#include "thread.h"

#include "sup.h"

/* libc */
#include <pthread.h>
#include <errno.h>

/* vbox */
#include <internal/thread.h>

static Genode::Cpu_session * get_cpu_session(RTTHREADTYPE type) {
	using namespace Genode;

	static Cpu_connection * con[RTTHREADTYPE_END - 1];
	static Lock lock;

	Assert(type && type < RTTHREADTYPE_END);

	Lock::Guard guard(lock);

	if (con[type - 1])
		return con[type - 1];

	unsigned const VIRTUAL_GENODE_VBOX_LEVELS = 16;
	static_assert (RTTHREADTYPE_END < VIRTUAL_GENODE_VBOX_LEVELS,
	               "prio levels exceeds VIRTUAL_GENODE_VBOX_LEVELS");

	long const prio = (VIRTUAL_GENODE_VBOX_LEVELS - type) *
	                  Cpu_session::PRIORITY_LIMIT / VIRTUAL_GENODE_VBOX_LEVELS;

	char * data = new (env()->heap()) char[16];

	snprintf(data, 16, "vbox %u", type);

	con[type - 1] = new (env()->heap()) Cpu_connection(data, prio);

	/* upgrade memory of cpu session for frequent used thread type */
	if (type == RTTHREADTYPE_IO)
		Genode::env()->parent()->upgrade(con[type - 1]->cap(), "ram_quota=4096");

	return con[type - 1];
}

/* HACK start */
#include <trace/timestamp.h>
#include <os/attached_rom_dataspace.h>
enum { MAX_PTHREAD_THREADS = 16 };

static unsigned                 thread_num = 0;
static pthread_t                threads[MAX_PTHREAD_THREADS];
static unsigned long long       times_l[MAX_PTHREAD_THREADS];
static unsigned long long       times_c[MAX_PTHREAD_THREADS];
static Genode::Trace::Timestamp timestamp_l;
static Genode::Trace::Timestamp timestamp_c;


extern "C" void pthread_dump()
{
	static unsigned long long freq_khz = 0;

	if (!freq_khz) {
		Genode::Attached_rom_dataspace _ds("hypervisor_info_page");
		Nova::Hip * const hip = _ds.local_addr<Nova::Hip>();
		freq_khz = hip->tsc_freq;
		PERR("freq %llu", freq_khz);
	}

	using Genode::snprintf;
	using Genode::printf;

	timestamp_c = Genode::Trace::timestamp();

	for (unsigned i=0; i < MAX_PTHREAD_THREADS; i++)
	{
		if (!threads[i])
			continue;

		using Genode::Thread_base;

		Thread_base * thread = dynamic_cast<Thread_base *>(threads[i]);

		static char name[Thread_base::Context::NAME_LEN];
		thread->name(name, sizeof(name));

		printf("%2u '%s' ", i, name);

		unsigned long long n_time = 0;
		uint8_t res = Nova::sc_ctrl(thread->tid().ec_sel + 2, n_time);
		if (res != Nova::NOVA_OK) {
			//PERR("sc cap not valid");
		}

		times_c[i] = n_time;
	}
	printf("\n");

	unsigned long long elapsed_ms = (timestamp_c - timestamp_l) / freq_khz;
	if (elapsed_ms == 0) elapsed_ms = 1;

	static char text[128];
	char * text_p = text;

	for (unsigned i = 0; i < MAX_PTHREAD_THREADS; i++) {

		if (!threads[i])
			continue;

		int w;

		if (times_c[i] <= times_l[i]) 
			w = snprintf(text_p, sizeof(text) - 1 - (text_p - text),
		                 "---.- ");
		else {
			unsigned long long t = (times_c[i] - times_l[i]) / elapsed_ms;
			w = snprintf(text_p, sizeof(text) - 1 - (text_p - text),
			             "%s%llu.%llu ",
			             (t / 10 >= 100 ? "" : (t / 10 >= 10 ? " " : "  ")),
			             t / 10, t % 10);
		}

		if (w <= 0) {
			PERR("idle output text error");
			nova_die();
		}
		text_p += w;
		times_l[i] = times_c[i];
	}
	*text_p = 0;

	printf("%8llu ms - %s\n", elapsed_ms, text);

	timestamp_l = timestamp_c;
}
/* HACK end */


extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
	                          void *(*start_routine) (void *), void *arg)
{
	PRTTHREADINT rtthread = reinterpret_cast<PRTTHREADINT>(arg);

	Assert(rtthread);

	size_t stack_size = Genode::Native_config::context_virtual_size() -
	                    sizeof(Genode::Native_utcb) - 2 * (1UL << 12);

	if (rtthread->cbStack < stack_size)
		stack_size = rtthread->cbStack;
	else
		PWRN("requested stack for thread '%s' of %zu Bytes is too large, "
		     "limit to %zu Bytes", rtthread->szName, rtthread->cbStack,
		     stack_size);

	/* sanity check - emt and vcpu thread have to have same prio class */
	if (!Genode::strcmp(rtthread->szName, "EMT"))
		Assert(rtthread->enmType == RTTHREADTYPE_EMULATION);

	if (rtthread->enmType == RTTHREADTYPE_EMULATION) {
		Genode::Cpu_session * cpu_session = get_cpu_session(RTTHREADTYPE_EMULATION);
/*
		Genode::Affinity::Space cpu_space = cpu_session->affinity_space();
		Genode::Affinity::Location location = cpu_space.location_of_index(i);
*/
		Genode::Affinity::Location location;
		if (create_emt_vcpu(thread, stack_size, attr, start_routine, arg,
		                    cpu_session, location)) {
	/* HACK start */
			threads[thread_num] = *thread;
			thread_num ++;
			Assert(thread_num < MAX_PTHREAD_THREADS);
	/* HACK end */
			return 0;
		}
		/* no haredware support, create normal pthread thread */
	}

	pthread_t thread_obj = new (Genode::env()->heap())
	                           pthread(attr ? *attr : 0, start_routine,
	                           arg, stack_size, rtthread->szName,
	                           get_cpu_session(rtthread->enmType));

	if (!thread_obj)
		return EAGAIN;

	*thread = thread_obj;

	thread_obj->start();

	/* HACK start */
	threads[thread_num] = thread_obj;

	using Genode::Thread_base;
	Thread_base * genodethread = dynamic_cast<Thread_base *>(thread_obj);

	/* request native SC cap */
	Genode::Native_capability pager_cap(genodethread->tid().ec_sel + 1);
	request_native_sc_cap(pager_cap, genodethread->tid().ec_sel + 2);

	thread_num ++;
	Assert(thread_num < MAX_PTHREAD_THREADS);

	/* HACK end */

	return 0;
}

extern "C" int pthread_attr_setdetachstate(pthread_attr_t *, int)
{
	return 0;
}

extern "C" int pthread_attr_setstacksize(pthread_attr_t *, size_t)
{
	return 0;
}

extern "C" int pthread_atfork(void (*)(void), void (*)(void), void (*)(void))
{
	return 0;
}
