/*
 * \brief  Interfaces for initializing libc subsystems
 * \author Norman Feske
 * \date   2016-10-27
 */

/*
 * Copyright (C) 2016-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__INIT_H_
#define _LIBC__INTERNAL__INIT_H_

/* Genode includes */
#include <base/env.h>
#include <base/heap.h>
#include <base/node.h>
#include <util/callable.h>
#include <vfs/types.h>  /* for 'MAX_PATH_LEN' */

/* libc includes */
#include <setjmp.h>     /* for 'jmp_buf' type */
#include <libc/component.h>

/* libc-internal includes */
#include <internal/types.h>
#include <internal/config.h>

namespace Genode::Vfs { struct Read_ready_response_handler; }

namespace Libc {

	struct Resume;
	struct Suspend;
	struct Monitor;
	struct Current_time;
	struct Current_real_time;
	struct Clone_connection;
	struct Watch;
	struct Signal;
	struct Fds;
	struct Timer_accessor;
	struct Cwd;
	struct Atexit;
	struct Vfs_plugin;
	struct Fs;

	/**
	 * Support for shared libraries
	 */
	void init_dl(Genode::Env &env);

	/**
	 * Global memory allocator
	 */
	void init_mem_alloc(Genode::Env &env);

	/**
	 * file system access
	 */
	void init_file_operations(Cwd &, Fds &, Fs &, Config const &);
	void init_pread_pwrite(Fds &);

	/**
	 * Poll support
	 */
	void init_poll(Signal &, Monitor &, Fds &);

	/**
	 * Support for querying available RAM quota in sysctl functions
	 */
	void sysctl_init(Genode::Env &env);

	/**
	 * Support for getpwent
	 */
	void init_passwd(Node const &);

	/**
	 * Support for getgrent
	 */
	void init_group(Node const &);

	/**
	 * Malloc allocator
	 */
	void init_malloc(Genode::Allocator &);
	void init_malloc_cloned(Clone_connection &);
	void reinit_malloc(Genode::Allocator &);

	using Rtc_path = String<Vfs::MAX_PATH_LEN>;

	/**
	 * Init timing facilities
	 */
	void init_sleep(Monitor &);
	void init_time(Current_time &, Current_real_time &);
	void init_alarm(Timer_accessor &, Signal &);

	/**
	 * Socket support
	 */
	void init_socket(Genode::Allocator &, Monitor &, Fds &, Config const &);
	void init_socket_operations(Genode::Allocator &, Fds &, Config const &);

	/**
	 * Pthread/semaphore support
	 */
	void init_pthread_support(Monitor &, Timer_accessor &);
	void init_pthread_support(Genode::Env &, Node const &, Genode::Allocator &);
	void init_semaphore_support(Timer_accessor &);

	/**
	 * Fork mechanism
	 */
	void init_fork(Genode::Env &, Fs &, Fds &,
	               Config_accessor const &, Genode::Allocator &heap,
	               Heap &malloc_heap, int pid, Monitor &, Signal &,
	               Binary_name const &);

	struct Reset_atexit : Interface
	{
		virtual void reset_atexit() = 0;
	};

	struct Reset_malloc_heap : Interface
	{
		virtual void reset_malloc_heap() = 0;
	};

	/**
	 * Execve mechanism
	 */
	void init_execve(Genode::Env &, Genode::Allocator &, void *user_stack,
	                 Reset_atexit &, Reset_malloc_heap &, Binary_name &,
	                 Fds &);

	/**
	 * Signal handling
	 */
	void init_signal(Signal &);

	/**
	 * Atexit handling
	 */
	void init_atexit(Atexit &);

	/**
	 * Kqueue support
	 */
	void init_kqueue(Genode::Allocator &, Monitor &, Fds &);

	/**
	 * Random-number support
	 */
	void init_random(Config const &);

	/**
	 * Set current binary name
	 */
	void update_dl_binary(Libc::Binary_name &);
}

#endif /* _LIBC__INTERNAL__INIT_H_ */
