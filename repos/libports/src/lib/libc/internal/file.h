/*
 * \brief  File-operation utilities
 * \author Christian Helmuth
 * \author Emery Hemingway
 * \date   2015-06-30
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__FILE_H_
#define _LIBC__INTERNAL__FILE_H_

/* Genode includes */
#include <base/log.h>

/* libc-internal includes */
#include <internal/fds.h>


enum { INVALID_FD = -1 };


static Libc::Fds *_fds_ptr;


static Libc::Fds &fds()
{
	if (!_fds_ptr) {
		Genode::error("missing initialization of _fds_ptr");
		for (;;);
	}
	return *_fds_ptr;
}


namespace Libc {

	template <typename FN>
	static inline auto with_fd(int libc_fd, char const *caller_name, FN const &fn)
	-> typename Trait::Functor<decltype(&FN::operator())>::Return_type
	{
		File_descriptor *fd_ptr = fds().with_space([&] (Fds::Space &space) {
			return space.apply<File_descriptor>({ unsigned(libc_fd) },
				[&] (File_descriptor &fd) {
					if (fd._ref_count && !fd._reacquire_warning_shown_once) {
						warning("attempt to re-acquire file descriptor for ", fd.path, " (", caller_name, ")");
						fd._reacquire_warning_shown_once = true;
					}
					fd._ref_count++;
					return &fd;
				},
				[&] { return nullptr; }); });

		if (!fd_ptr) {
			if (caller_name)
				error("unknown fd ", libc_fd, " passed to ", caller_name);
			return Errno(EBADF);
		}

		auto ret = fn(*fd_ptr);
		fd_ptr->_ref_count--;
		return ret;
	}

	static inline int with_open_file(int libc_fd, char const *caller_name, auto const &fn)
	{
		return with_fd(libc_fd, caller_name, [&] (File_descriptor &fd) {
			return fd.open_file_ptr ? fn(*fd.open_file_ptr) : int(Errno(EBADF)); });
	}


	static inline int with_open_dir(int libc_fd, char const *caller_name, auto const &fn)
	{
		return with_fd(libc_fd, caller_name, [&] (File_descriptor &fd) {
			return fd.open_dir_ptr ? fn(*fd.open_dir_ptr) : int(Errno(EBADF)); });
	}

	static inline bool fd_in_use(int libc_fd)
	{
		return fds().with_alloc([&] (Fds::Bits &bits, Fds::Space &) {
			return bits.alloc_addr(libc_fd).convert<bool>(
				[&] (Ok) { bits.free(libc_fd); return false; },
				[&] (Fds::Bits::Error) {       return true; }); });
	}

	bool read_ready_from_kernel (File_descriptor &);
	bool write_ready_from_kernel(File_descriptor &);
}

#endif /* _LIBC__INTERNAL__FILE_H_ */
