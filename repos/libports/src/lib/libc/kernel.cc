/*
 * \brief  Libc kernel for main and pthreads user contexts
 * \author Christian Helmuth
 * \author Emery Hemingway
 * \author Norman Feske
 * \date   2016-01-22
 */

/*
 * Copyright (C) 2016-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* libc-internal includes */
#include <internal/kernel.h>
#include <internal/file_operations.h>

/* libc includes */
extern "C" {
#include <fcntl.h>
}


Libc::Kernel * Libc::Kernel::_kernel_ptr;


extern char **environ;

char const *libc_resolv_path; /* expected by res_init.c */

/**
 * Blockade for main context
 */

inline void Libc::Main_blockade::block()
{
	Check check { _woken_up };

	do {
		_timeout_ms = Kernel::kernel().suspend(check, _timeout_ms);
		_expired    = _timeout_valid && !_timeout_ms;
	} while (!woken_up() && !expired());
}

inline void Libc::Main_blockade::wakeup()
{
	_woken_up = true;
	Kernel::kernel().resume_main();
}


void Libc::Kernel::reset_malloc_heap()
{
	_malloc_ram.construct(_heap, _env.ram());

	_cloned_heap_ranges.for_each([&] (Registered<Cloned_malloc_heap_range> &r) {
		destroy(_heap, &r); });

	Heap &raw_malloc_heap = *_malloc_heap;
	construct_at<Heap>(&raw_malloc_heap, *_malloc_ram, _env.rm());

	reinit_malloc(raw_malloc_heap);
}


void Libc::Kernel::_init_file_descriptors()
{
	/* guard used to print an offending libc config when leaving the scope */
	struct Diag_guard
	{
		Kernel &kernel;
		bool show = false;

		Diag_guard(Kernel &kernel) : kernel(kernel) { }

		~Diag_guard() { if (show) log(kernel._config_rom.node()); }

	} diag_guard { *this };

	using Path = Fs::Target_path;

	auto follow_symlinks = [&] (Path path) -> Fs::Follow_result
	{
		Fs::Follow_result result = Fs::Follow_error { };

		unsigned follow_limit = 10;

		for (bool done = false; !done && follow_limit; follow_limit--) {

			char target[Vfs::MAX_PATH_LEN] { };
			Vfs::Follow_result follow = Vfs::Follow_error::RETRY;
			for (;;) {
				follow = _vfs_root.fs().follow(path.string(), { target, sizeof(target) });
				if (follow != Vfs::Follow_error::RETRY) break;
				_vfs_root.io().commit();
			}

			result = Fs::deref_symlink(path.string(), follow, Cstring(target))
				.convert<Fs::Follow_result>(
					[&] (Fs::Target_path const &p)   { return p; },
					[&] (Fs::Deref_symlink_too_long) { return Fs::Follow_error::TOO_LONG; });
			if (result.failed())
				break;

			result.with_result(
				[&] (Path const &resolved) {
					done = (path == resolved);
					path = resolved;
				},
				[&] (Fs::Follow_error) { done = true; });
		}

		if (follow_limit == 0) errno = ELOOP;

		return result;
	};

	auto init_fd = [&] (Node const &node, char const *attr,
	                    int libc_fd, unsigned flags)
	{
		if (!node.has_attribute(attr))
			return;

		Path path { node.attribute_value(attr, Path()) };
		{
			Fs::Follow_result result = follow_symlinks(path);
			if (result.failed()) {
				warning("failed to resolve path for ", attr, ": ", path);
				diag_guard.show = true;
				return;
			}
			result.with_result(
				[&] (Path const &p) { path = p; },
				[&] (Fs::Follow_error) { });
		}

		struct stat out_stat { };
		if (_fs.stat_from_kernel(path.string(), out_stat) != 0) {
			warning("failed to call 'stat' on ", path);
			diag_guard.show = true;
			return;
		}

		bool const writeable = (flags & O_ACCMODE) != O_RDONLY;

		_fds.with_alloc([&] (Fds::Bits &bits, Fds::Space &space) {
			bits.alloc_addr(libc_fd).with_result(
				[&] (Ok) {
					_fs.open_file_from_kernel({
						.path = path.string(),
						.writeable = writeable
					}).with_result(
						[&] (Open_file &of) {
							File_descriptor &new_fd = *new (_heap)
								File_descriptor(space, libc_fd, of, path.string());

							_procfs.add_fd_file_from_kernel(libc_fd, path);

							new_fd.flags   = flags;
							new_fd.cloexec = node.attribute_value("cloexec", false);

							::off_t const seek = node.attribute_value("seek", 0ULL);
							if (seek)
								_fs.lseek_from_kernel(new_fd, seek);

							if (of.handle.attach() == Vfs::File_handle::Attach_error::RETRY)
								warning("libc init fd for ", path.string(), " not yet attached");
						},
						[&] (Errno) {
							error("could not open fd ", libc_fd, " for ", path);
						});
				},
				[&] (Fds::Bits::Error) {
					error("could not allocate fd ", libc_fd, " for ", path);
					diag_guard.show = true;
				});
		});
	};

	bool const fs_has_dirents = _vfs_root.fs().num_dirent("/") > 0;
	if (fs_has_dirents) {

		_config_rom.node().with_optional_sub_node("libc", [&] (Node const &node) {

			using Path = String<Vfs::MAX_PATH_LEN>;

			if (node.has_attribute("cwd")) {
				Absolute_path abs_path { };
				abs_path.import(node.attribute_value("cwd", Path()).string(), _cwd.string());
				_cwd = { abs_path.string() };
			}

			init_fd(node, "stdin",  0, O_RDONLY);
			init_fd(node, "stdout", 1, O_WRONLY);
			init_fd(node, "stderr", 2, O_RDWR);

			node.for_each_sub_node("fd", [&] (Node const &fd) {

				unsigned const id = fd.attribute_value("id", 0U);

				bool const rd = fd.attribute_value("readable",  false);
				bool const wr = fd.attribute_value("writeable", false);
				bool const nb = fd.attribute_value("nonblocking", false);

				unsigned const accmode = rd ? (wr ? O_RDWR : O_RDONLY)
				                            : (wr ? O_WRONLY : 0);
				unsigned const flags   = (nb ? O_NONBLOCK : 0) | accmode;

				if (!fd.has_attribute("path")) {
					if (fd.attribute_value("type", String<8>()) == "kqueue")
						warning("kqueue fd in libc config is currently not supported");
					else {
						warning("unknown path for file descriptor ", id);
						diag_guard.show = true;
					}
				}

				init_fd(fd, "path", id, flags);
			});
		});

		/* prevent use of IDs of stdin, stdout, and stderr for other files */
		_fds.with_alloc([&] (Fds::Bits &bits, Fds::Space &) {
			for (unsigned fd = 0; fd <= 2; fd++)
				(void)bits.alloc_addr(fd); });
	}

	/**
	 * Call 'fn' with root directory and path to ioctl pseudo file as arguments
	 *
	 * If no matching ioctl pseudo file exists, 'fn' is not called.
	 */
	auto with_ioctl_path = [&] (int libc_fd, char const *file, auto fn)
	{
		Absolute_path path { };
		_fds.with_space([&] (Fds::Space &space) {
			return space.apply<File_descriptor>({ unsigned(libc_fd) },
				[&] (File_descriptor &fd) {
					path = { fd.ioctl_dir().string() };;
					path.append_element(file);
				},
				[&] { }); });

		if (_root_dir.file_exists(path.string()))
			fn(_root_dir, path.string());
	};

	/*
	 * Watch stdout's 'info' pseudo file to detect terminal-resize events
	 */
	with_ioctl_path(STDOUT_FILENO, "info", [&] (Directory &root_dir, char const *path) {
		_terminal_resize_handler.construct(root_dir, path, *this,
		                                   &Kernel::_handle_terminal_resize);
		(void)_terminal_resize_handler->watch();
	});

	/*
	 * Watch stdin's 'interrupts' pseudo file to detect control-c events
	 */
	with_ioctl_path(STDIN_FILENO, "interrupts", [&] (Directory &root_dir, char const *path) {
		_user_interrupt_handler.construct(root_dir, path,
		                                  *this, &Kernel::_handle_user_interrupt);
		(void)_user_interrupt_handler->watch();
	});
}


void Libc::Kernel::_handle_terminal_resize()
{
	_signal.charge(SIGWINCH);
	_resume_main();
}


void Libc::Kernel::_handle_user_interrupt()
{
	_signal.charge(SIGINT);
	_resume_main();
}


void Libc::Kernel::_clone_state_from_parent()
{
	using Range = Region_map::Range;

	auto range_attr = [&] (Node const &node)
	{
		return Range {
			.start     = node.attribute_value("at",   0UL),
			.num_bytes = node.attribute_value("size", 0UL)
		};
	};

	/*
	 * Allocate local memory for the backing store of the application heap,
	 * mirrored from the parent.
	 *
	 * This step must precede the creation of the 'Clone_connection' because
	 * the shared-memory buffer of the clone session may otherwise potentially
	 * interfere with such a heap region.
	 */
	_with_libc_config([&] (Node const &libc) {
		libc.for_each_sub_node("heap", [&] (Node const &node) {
			Range const range = range_attr(node);
			new (_heap)
				Registered<Cloned_malloc_heap_range>(_cloned_heap_ranges,
				                                     _env.ram(), _env.rm(),
				                                     range); });
	});

	_clone_connection.construct(_env);

	/* fetch heap content */
	_cloned_heap_ranges.for_each([&] (Cloned_malloc_heap_range &heap_range) {
		heap_range.import_content(*_clone_connection); });

	/* value of global environ pointer (the env vars are already on the heap) */
	_clone_connection->memory_content(&environ, sizeof(environ));

	/* fetch user contex of the parent's application */
	_clone_connection->memory_content(&_user_context, sizeof(_user_context));
	_clone_connection->memory_content(&_main_monitor_job, sizeof(_main_monitor_job));
	_valid_user_context = true;

	auto copy_from_parent = [&] (Range range)
	{
		_clone_connection->memory_content((void *)range.start, range.num_bytes);
	};

	_with_libc_config([&] (Node const &libc) {
		libc.for_each_sub_node([&] (Node const &node) {

			/* clone application stack */
			if (node.type() == "stack")
				copy_from_parent(range_attr(node));

			/* clone RW segment of a shared library or the binary */
			if (node.type() == "rw") {
				using Name = String<64>;
				Name const name = node.attribute_value("name", Name());

				/*
				 * The blacklisted segments are initialized via the
				 * regular startup of the child.
				 */
				bool const blacklisted = (name == "ld.lib.so")
				                      || (name == "libc.lib.so")
				                      || (name == "libm.lib.so")
				                      || (name == "posix.lib.so")
				                      || (strcmp(name.string(), "vfs", 3) == 0);
				if (!blacklisted)
					copy_from_parent(range_attr(node));
			}
		});
	});

	/* import application-heap state from parent */
	_clone_connection->object_content(_malloc_heap);
	init_malloc_cloned(*_clone_connection);
}


void Libc::Kernel::handle_io_progress()
{
	if (_io_progressed) {
		_io_progressed = false;

		Kernel::resume_all();

		if (_execute_monitors_pending == Monitor::Pool::State::JOBS_PENDING)
			_execute_monitors_pending = _monitors.execute_monitors();
	}

	wakeup_remote_peers();
}


void Libc::execute_in_application_context(Application_code &app_code)
{
	/*
	 * The libc execution model builds on the main entrypoint, which handles
	 * all relevant signals (e.g., timing and VFS). Additional component
	 * entrypoints or pthreads should never call with_libc() but we catch this
	 * here and just execute the application code directly.
	 */
	if (!Kernel::kernel().main_context()) {
		app_code.execute();
		Kernel::kernel().wakeup_remote_peers();
		return;
	}

	static bool nested = false;

	if (nested) {

		if (Kernel::kernel().main_suspended()) {
			Kernel::kernel().nested_execution(app_code);
		} else {
			app_code.execute();
		}
		return;
	}

	nested = true;
	Kernel::kernel().run(app_code);
	nested = false;

	Kernel::kernel().wakeup_remote_peers();
}


static Libc::Fds *_atexit_fds_ptr;


static void close_file_descriptors_on_exit()
{
	using namespace Libc;
	for (;;) {
		int libc_fd = -1;
		_atexit_fds_ptr->with_space([&] (Fds::Space &space) {
			space.apply_any<File_descriptor>([&] (File_descriptor &fd) {
				libc_fd = fd.libc_fd; }); });
		if (libc_fd == -1)
			break;

		close(libc_fd);
	}
}


void Libc::Kernel::reset_atexit()
{
	_atexit.reset_handlers();
	atexit(close_file_descriptors_on_exit);
}


Libc::Kernel::Kernel(Genode::Env &env, Genode::Allocator &heap)
:
	_env(env), _heap(heap)
{
	libc_resolv_path = _config.nameserver.string();

	init_atexit(_atexit);

	_atexit_fds_ptr = &_fds;
	atexit(close_file_descriptors_on_exit);

	init_semaphore_support(_timer_accessor);
	init_pthread_support(*this, _timer_accessor, _heap);

	_with_libc_sub_config("pthread", [&] (Node const &pthread_config) {
		init_pthread_support(env, pthread_config, _heap); });

	_fds.init_pthread_mutex();

	_env.ep().register_io_progress_handler(*this);

	if (_config.cloned) {
		_clone_state_from_parent();

	} else {
		_malloc_heap.construct(*_malloc_ram, _env.rm());
		init_malloc(*_malloc_heap);
	}

	init_fork(_env, _fs, _fds, _libc_env, _heap, *_malloc_heap, _config.pid,
	          _config.uid, _config.gid, *this, _signal, _binary_name);
	init_execve(_env, _heap, _user_stack, *this, *this, _binary_name, _fds, _fs);
	init_sleep(*this);
	init_file_operations(*this, _fds, _fs, _config, _procfs);
	init_pread_pwrite(_fds);
	init_time(*this, *this);
	init_alarm(_timer_accessor, _signal);
	init_poll(_signal, *this, _fds);
	init_socket(_heap, *this, _fds, _fs, _config);
	init_socket_operations(_heap, _fds, _config);

	_with_libc_sub_config("passwd", [&] (Node const &passwd_config) {
		init_passwd(passwd_config); });

	_with_libc_sub_config("group", [&] (Node const &group_config) {
		init_group(group_config); });

	init_signal(_signal);
	init_kqueue(_heap, *this, _fds);
	init_random(_config);

	_init_file_descriptors();

	_kernel_ptr = this;

	/*
	 * Acknowledge the completion of 'fork' to the parent
	 *
	 * This must be done after '_init_file_descriptors' to ensure that pipe FDs
	 * of the parent are opened at the child before the parent continues.
	 * Otherwise, the parent would potentially proceed with closing the pipe
	 * FDs before the child has a chance to open them. In this situation, the
	 * pipe reference counter may reach an intermediate value of zero,
	 * triggering the destruction of the pipe.
	 */
	if (_config.cloned)
		_clone_connection.destruct();
}
