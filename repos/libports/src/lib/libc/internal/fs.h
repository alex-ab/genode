/*
 * \brief  Libc access to component-local virtual file system
 * \author Norman Feske
 * \date   2026-06-19
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__FS_H_
#define _LIBC__INTERNAL__FS_H_

/* Genode includes */
#include <os/vfs.h>
#include <vfs/file_system.h>

/* libc includes */
extern "C" {
#include <sys/stat.h>
}

/* libc-internal includes */
#include <internal/types.h>
#include <internal/poll.h>
#include <internal/errno.h>

namespace Libc {

	struct Fs;
	struct Monitor;
	struct Config;
	struct Current_real_time;

	struct Open_file : Noncopyable
	{
		Vfs::Vfs_handle &handle;

		uint64_t pos = 0;       /* read/write position */

		bool modified = false;  /* fsync is needed */
		bool blocking = false;  /* read is blocking */
		bool closing  = false;

		Open_file(Vfs::Vfs_handle &handle) : handle(handle) { }
	};

	struct Open_dir : Noncopyable
	{
		using Path = String<Vfs::MAX_PATH_LEN>;

		Vfs::Vfs_handle &handle;

		Path const path;  /* for computing 'fileno' values in getdirentries */

		uint64_t pos = 0; /* read position */

		Open_dir(Vfs::Vfs_handle &handle, Path const &path)
		: handle(handle), path(path) { }
	};

	struct Fs;
}


struct Libc::Fs
{
	Monitor                          &_monitor;
	Genode::Env::Local_rm            &_local_rm;
	Vfs::Read_ready_response_handler &_response_handler;
	Config                     const &_config;
	Current_real_time                &_now;
	Genode::Allocator                &_kernel_heap;
	Vfs::File_system                 &_vfs;
	Directory                        &_root_dir;

	using off_t  = ::off_t;
	using size_t = ::size_t;
	using At = Vfs::At;

	void destroy_vfs_handle(Vfs::Vfs_handle &);

	int     access  (char const *, int);
	int     stat    (char const *, struct stat &);
	int     mkdir   (char const *, mode_t);
	ssize_t readlink(char const *, char *, size_t);
	int     rename  (char const *, char const *);
	int     symlink (char const *, char const *);
	int     unlink  (char const *);

	static int poll(Monitor &, Pollfd fds[], int nfds);

	void fsync    (Open_file &);
	int  ftruncate(Open_file &, off_t);

	using Open_file_result = Unique_attempt<Open_file &, Errno>;
	using Open_dir_result  = Unique_attempt<Open_dir  &, Errno>;

	Open_file_result open_file  (const char *path, int flags);
	Open_file_result create_file(const char *path, int flags);
	Open_dir_result  open_dir   (const char *path, int flags);

	void destroy(Open_file &);
	void destroy(Open_dir &);

	int pipe(File_descriptor *pipefdo[2]);

	ssize_t getdirentries(Open_dir &, char *, size_t , off_t *);

	int     fstat(File_descriptor &, struct stat &);
	off_t   lseek(File_descriptor &, off_t offset, int whence);
	int     fcntl(File_descriptor &, int, long);
	ssize_t read (File_descriptor &, void *, size_t);
	ssize_t write(File_descriptor &, void const *, size_t );
	void   *mmap (File_descriptor &, void *, size_t, int, int, off_t);

	int wait_aio     (File_descriptor &, int timeout_ms);
	int enqueue_aiocb(File_descriptor &, struct aiocb const &);

	int munmap(void *, size_t);

	int ioctl(File_descriptor &, unsigned long request, char *argp);

	/* kernel-specific API without monitor */
	Open_file_result open_file_from_kernel(const char *, int);
	int stat_from_kernel(char const *, struct stat &);
	void lseek_from_kernel(File_descriptor &, off_t offset);
};

#endif /* _LIBC__INTERNAL__FS_H_ */
