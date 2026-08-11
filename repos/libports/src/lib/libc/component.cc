/*
 * \brief  Libc component startup
 * \author Christian Helmuth
 * \author Norman Feske
 * \date   2016-01-22
 */

/*
 * Copyright (C) 2016-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>

/* libc-internal includes */
#include <internal/kernel.h>


extern char **environ;


Genode::size_t Component::stack_size() { return Libc::Component::stack_size(); }


void Component::construct(Genode::Env &env)
{
	/* initialize the global pointer to environment variables */
	static char *null_env = nullptr;
	if (!environ) environ = &null_env;

	/* call of '__cxa_atexit' is ignored prior 'init_atexit' */
	static Genode::Heap heap { env.ram(), env.rm() };

	/* pass Genode::Env to libc subsystems that depend on it */
	Libc::init_mem_alloc(env);
	Libc::init_dl(env);
	Libc::sysctl_init(env);

	/* prevent call of '__cxa_atexit' for the 'Kernel' object */
	static long _kernel_obj[(sizeof(Libc::Kernel) + sizeof(long))/sizeof(long)];
	Libc::Kernel &kernel = *new (_kernel_obj) Libc::Kernel(env, heap);

	/* finish static construction of component and libraries */
	Libc::with_libc([&] () { env.exec_static_constructors(); });

	/* construct libc component on kernel stack */
	Libc::Component::construct(kernel.libc_env());
}


/**
 * Default stack size for libc-using components
 */
Genode::size_t Libc::Component::stack_size() __attribute__((weak));
Genode::size_t Libc::Component::stack_size() { return 32UL*1024*sizeof(long); }
