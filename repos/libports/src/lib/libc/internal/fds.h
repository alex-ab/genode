/*
 * \brief  File descriptor definition and lifetime management
 * \author Norman Feske
 * \date   2026-06-23
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__FDS_H_
#define _LIBC__INTERNAL__FDS_H_

/* Genode includes */
#include <base/mutex.h>
#include <base/allocator.h>
#include <base/id_space.h>
#include <util/bit_allocator.h>
#include <vfs/vfs_handle.h>

/* libc-internal includes */
#include <internal/kqueue.h>
#include <internal/pthread.h>
#include <internal/socket.h>
#include <internal/fs.h>

enum { MAX_NUM_FDS = 1024 };

namespace Libc {

	struct Open_file;
	struct Open_dir;
	struct Kqueue;
	struct Socket;
	struct Fs;

	enum { ANY_FD = -1 };

	class Fds;

	struct File_descriptor;
}


class Libc::Fds
{
	public:

		using Bits  = Bit_allocator<MAX_NUM_FDS>;
		using Space = Id_space<File_descriptor>;

	private:

		Bits  _bits  { };
		Space _space { };

		/*
		 * pthread support is not initialized yet at 'Fds' construction time,
		 * so the mutex is constructed later via 'init_pthread_mutex()'.
		 */
		Constructible<Pthread_mutex> _mutex { };

	public:

		void init_pthread_mutex()
		{
			_mutex.construct();
		}

		template <typename FN>
		auto with_alloc(FN const &fn)
		-> typename Trait::Functor<decltype(&FN::operator())>::Return_type
		{
			Pthread_mutex::Guard guard { *_mutex };
			return fn(_bits, _space);
		}

		template <typename FN>
		auto with_space(FN const &fn)
		-> typename Trait::Functor<decltype(&FN::operator())>::Return_type
		{
			Pthread_mutex::Guard guard { *_mutex };
			return fn(_space);
		}
};


struct Libc::File_descriptor
{
	Genode::Mutex mutex { };

	Fds::Space::Element _elem;

	int const libc_fd = _elem.id().value;

	Open_file * const open_file_ptr = nullptr;
	Open_dir  * const open_dir_ptr  = nullptr;
	Socket    * const socket_ptr    = nullptr;
	Kqueue    * const kqueue_ptr    = nullptr;

	using Path = String<Vfs::MAX_PATH_LEN>;

	Path path;

	unsigned _ref_count = 0;

	bool _reacquire_warning_shown_once = false;

	struct Aio_handle
	{
		enum class State { INVALID, QUEUED, COMPLETE };

		State state = State::INVALID;

		Open_file *of_ptr = nullptr;

		bool used = false;

		::size_t count  = 0;
		::off_t  offset = 0;

		void with_open_file(auto const &fn)
		{
			if (of_ptr)
				fn(*of_ptr);
		}

		void reset()
		{
			used   = false;
			count  = 0;
			offset = 0;
			state  = State::INVALID;
		}
	};

	static constexpr unsigned MAX_VFS_HANDLES_PER_FD = 64;
	Aio_handle _aio_handles[MAX_VFS_HANDLES_PER_FD] { };

	void any_unused_aio_handle(auto const &fn)
	{
		for (unsigned i = 0; i < MAX_VFS_HANDLES_PER_FD; i++)
			if (!_aio_handles[i].used) {
				fn(_aio_handles[i]);
				break;
			}
	}

	void close_aio_handles(Fs &fs)
	{
		for (auto & handle : _aio_handles)
			if (handle.of_ptr) {
				fs.destroy(*handle.of_ptr);
				handle.of_ptr = nullptr;
			}
	}

	struct Aio_job
	{
		enum class State { FREE, PENDING, IN_PROGRESS, COMPLETE };

		const struct aiocb *iocb = nullptr;

		Aio_handle *handle = nullptr;
		ssize_t     result = -1;
		int         error  = 0;
		State       state  = State::FREE;

		void acquire_handle(Aio_handle &aio_handle)
		{
			handle       = &aio_handle;
			handle->used = true;
		}

		void release_handle()
		{
			if (!handle)
				return;

			handle->reset();
			handle = nullptr;
		}

		void with_aio_handle(auto const &handle_fn)
		{
			if (handle)
				handle_fn(*handle);
		}

		void free()
		{
			handle = nullptr;
			iocb   = nullptr;
			error  = 0;
			result = -1;
			state  = State::FREE;
		}
	};

	static constexpr unsigned MAX_AIOCB_PER_FD = MAX_VFS_HANDLES_PER_FD;
	Aio_job _aio_jobs[MAX_AIOCB_PER_FD] { };

	void for_each_aio_job(Aio_job::State state, auto const &fn)
	{
		for (unsigned i = 0; i < MAX_AIOCB_PER_FD; i++)
			if (_aio_jobs[i].state == state)
				fn(_aio_jobs[i]);
	}

	bool any_free_aio_job(auto const &fn)
	{
		for (unsigned i = 0; i < MAX_AIOCB_PER_FD; i++)
			if (_aio_jobs[i].state == Aio_job::State::FREE) {
				fn(_aio_jobs[i]);
				return true;
			}

		return false;
	}

	static void apply_lio(auto /* const or non-const */ &fd,
	                      struct aiocb const *iocb, auto const &fn)
	{
		for (unsigned i = 0; i < MAX_AIOCB_PER_FD; i++)
			if (iocb == fd._aio_jobs[i].iocb)
				fn(fd._aio_jobs[i]);
	}

	unsigned lio_list_completed = 0;
	unsigned lio_list_queued    = 0;

	int  flags   = 0;
	bool cloexec = false;
	bool closed  = false;

	File_descriptor(Fds::Space &space, int libc_fd, Open_file &of, Path const &path)
	: _elem(*this, space, { addr_t(libc_fd) }), open_file_ptr(&of), path(path) { }

	File_descriptor(Fds::Space &space, int libc_fd, Open_dir &od, Path const &path)
	: _elem(*this, space, { addr_t(libc_fd) }), open_dir_ptr(&od), path(path) { }

	File_descriptor(Fds::Space &space, int libc_fd, Socket &socket, Path const &path)
	: _elem(*this, space, { addr_t(libc_fd) }), socket_ptr(&socket), path(path) { }

	File_descriptor(Fds::Space &space, int libc_fd, Kqueue &kqueue)
	: _elem(*this, space, { addr_t(libc_fd) }), kqueue_ptr(&kqueue) { }

	~File_descriptor()
	{
		if (!closed) error("destructing unclosed file descriptor for ", path);
	}

	/**
	 * Return path to pseudo files used for ioctl operations of a given FD
	 */
	Path ioctl_dir() const;
};

#endif /* _LIBC__INTERNAL__FDS_H_ */
