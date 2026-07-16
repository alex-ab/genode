/*
 * \brief  libc file operations
 * \author Christian Prochaska
 * \author Norman Feske
 * \author Emery Hemingway
 * \author Christian Helmuth
 * \date   2010-01-21
 */

/*
 * Copyright (C) 2010-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/env.h>
#include <os/path.h>
#include <util/token.h>

/* compiler includes */
#include <stdarg.h>

extern "C" {
/* libc includes */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/param.h> /* PAGE_SHIFT */
#include <unistd.h>
#include <aio.h>
#include <libc_private.h>
#include <sys/cdefs.h>
}

/* libc-internal includes */
#include <internal/file.h>
#include <internal/file_operations.h>
#include <internal/mem_alloc.h>
#include <internal/mmap_registry.h>
#include <internal/errno.h>
#include <internal/init.h>
#include <internal/cwd.h>
#include <internal/config.h>

using namespace Libc;

#define __SYS_(ret_type, name, args, body) \
	extern "C" {\
	ret_type  __sys_##name args body \
	ret_type __libc_##name args __attribute__((alias("__sys_" #name))); \
	ret_type       _##name args __attribute__((alias("__sys_" #name))); \
	ret_type          name args __attribute__((alias("__sys_" #name))); \
	} \

Libc::Mmap_registry &Libc::mmap_registry()
{
	static Mmap_registry registry;
	return registry;
}

static Libc::Cwd          *_cwd_ptr;
static Libc::Fs           *_fs_ptr;
static Libc::Config const *_config_ptr;


void Libc::init_file_operations(Cwd &cwd, Fds &fds, Fs &fs, Config const &config)
{
	_fds_ptr    = &fds;
	_cwd_ptr    = &cwd;
	_fs_ptr     = &fs;
	_config_ptr = &config;
}


static Libc::Fs &fs()
{
	struct Missing_call_of_init_file_operations : Exception { };
	if (!_fs_ptr)
		throw Missing_call_of_init_file_operations();

	return *_fs_ptr;
}


static Libc::Config const &config()
{
	struct Missing_call_of_init_file_operations : Exception { };
	if (!_config_ptr)
		throw Missing_call_of_init_file_operations();

	return *_config_ptr;
}


/**
 * Current working directory
 */
static Libc::Cwd_path &cwd()
{
	struct Missing_call_of_init_file_operations : Exception { };
	if (!_cwd_ptr)
		throw Missing_call_of_init_file_operations();

	return _cwd_ptr->cwd();
}


using Path_element_token = Token<Vfs::Scanner_policy_path_element>;


/*
 * Resolve a symbolic link.
 *
 * Only the last element of the given absolute path gets resolved and it is
 * expected to be a symbolic link.
 */
static Symlink_resolve_result _resolve_symlink(Absolute_path const &path,
                                               Absolute_path &resolved_path)
{
	char symlink_target[PATH_MAX];
	Absolute_path tmp_resolved_path;
	int res;

	res = fs().readlink(path.base(), symlink_target, sizeof(symlink_target));
	if (res < 1)
		return Symlink_resolve_error();

	if (res == PATH_MAX) {
		errno = ENAMETOOLONG;
		return Symlink_resolve_error();
	}

	/* zero terminate target */
	symlink_target[res] = 0;

	try {
		if (symlink_target[0] == '/')
			/* absolute target */
			tmp_resolved_path.import(symlink_target, cwd().string());
		else {
			/* relative target */
			tmp_resolved_path = path;
			tmp_resolved_path.strip_last_element();
			tmp_resolved_path.append_element(symlink_target);
		}
	} catch (Path_base::Path_too_long) {
		errno = ENAMETOOLONG;
		return Symlink_resolve_error();
	}

	resolved_path = tmp_resolved_path;

	return Ok();
}


/**
 * Resolve symbolic links in a given absolute path
 */
Symlink_resolve_result Libc::resolve_symlinks(char const *path, Absolute_path &resolved_path)
{
	Absolute_path current_iteration_working_path;
	Absolute_path next_iteration_working_path(path, cwd().string());

	enum { FOLLOW_LIMIT = 10 };
	int follow_count = 0;
	bool symlink_resolved_in_this_iteration;
	do {
		if (follow_count++ == FOLLOW_LIMIT) {
			errno = ELOOP;
			return Symlink_resolve_error();
		}

		current_iteration_working_path = next_iteration_working_path;

		next_iteration_working_path.import("");
		symlink_resolved_in_this_iteration = false;

		Path_element_token t(current_iteration_working_path.base());

		while (t) {
			if (t.type() != Path_element_token::IDENT) {
					t = t.next();
					continue;
			}

			char path_element[PATH_MAX];

			t.string(path_element, sizeof(path_element));

			try {
				next_iteration_working_path.append_element(path_element);
			} catch (Path_base::Path_too_long) {
				errno = ENAMETOOLONG;
				return Symlink_resolve_error();
			}

			/*
			 * If a symlink has been resolved in this iteration, the remaining
			 * path elements get added and a new iteration starts.
			 */
			if (!symlink_resolved_in_this_iteration) {
				struct stat stat_buf;
				int res = fs().stat(next_iteration_working_path.base(), stat_buf);
				if (res == -1)
					return Symlink_resolve_error();
				if (S_ISLNK(stat_buf.st_mode)) {
					if (_resolve_symlink(next_iteration_working_path,
					                     next_iteration_working_path).failed())
						return Symlink_resolve_error();
					symlink_resolved_in_this_iteration = true;
				}
			}

			t = t.next();
		}

	} while (symlink_resolved_in_this_iteration);

	resolved_path = next_iteration_working_path;
	resolved_path.remove_trailing('/');

	return Ok();
}


static Symlink_resolve_result resolve_symlinks_except_last_element(char const *path, Absolute_path &resolved_path)
{
	Absolute_path absolute_path_without_last_element(path, cwd().string());
	absolute_path_without_last_element.strip_last_element();

	if (resolve_symlinks(absolute_path_without_last_element.base(), resolved_path).failed())
		return Symlink_resolve_error();

	/* append last element to resolved path */

	Absolute_path absolute_path_last_element(path, cwd().string());
	absolute_path_last_element.keep_only_last_element();
	/* the last element can have a trailing "/" if 'path' is "." */
	absolute_path_last_element.remove_trailing('/');

	try {
		resolved_path.append_element(absolute_path_last_element.base());
	} catch (Path_base::Path_too_long) {
		errno = ENAMETOOLONG;
		return Symlink_resolve_error();
	}

	return Ok();
}


/********************
 ** Libc functions **
 ********************/

extern "C" int access(const char *path, int amode)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks(path, resolved_path).failed()) {
		errno = ENOENT;
		return -1;
	}

	return fs().access(resolved_path.base(), amode);
}


extern "C" int chdir(const char *path)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	struct stat stat_buf;
	if ((stat(path, &stat_buf) == -1) ||
	    (!S_ISDIR(stat_buf.st_mode))) {
		errno = ENOTDIR;
		return -1;
	}
	Genode::Path<Vfs::MAX_PATH_LEN> new_path { };
	new_path.import(path, cwd().string());

	cwd() = { new_path.string() };
	return 0;
}


__SYS_(int, close, (int libc_fd),
{
	File_descriptor *fd_ptr = nullptr;

	int ret = with_fd(libc_fd, nullptr /* silent */, [&] (File_descriptor &fd) {

		if (fd.open_file_ptr) {
			if (fd.flags & O_CREAT) fd.open_file_ptr->modified = true; /* charge fsync */
			fs().destroy(*fd.open_file_ptr);
		}
		if (fd.open_dir_ptr) fs().destroy(*fd.open_dir_ptr);
		if (fd.socket_ptr)   destroy_socket(*fd.socket_ptr);
		if (fd.kqueue_ptr)   destroy_kqueue(*fd.kqueue_ptr);
		fd.close_aio_handles();
		fd.closed = true;

		/*
		 * Prevent fs from becoming ever acquired again, even after the final
		 * decrement by 'with_fd'.
		 */
		fd._ref_count = ~0u;
		fd_ptr = &fd;
		return 0;
	});

	if (fd_ptr) destroy(fs()._kernel_heap, fd_ptr);

	if (ret == 0)
		fds().with_alloc([&] (Fds::Bits &bits, Fds::Space &) {
			bits.free(libc_fd); });

	return ret;
})


static int _dup(File_descriptor &fd, Fds::Bits &bits, Fds::Space &space, int new_id)
{
	/* clear flags to prevent double create if 'fd' refers to a created file */
	int const new_flags = fd.flags & ~(O_EXCL | O_CREAT);

	auto new_file_descriptor = [&] (auto &open_file_or_dir)
	{
		File_descriptor &new_fd =
			*new (fs()._kernel_heap) File_descriptor(space, new_id, open_file_or_dir, fd.path);

		new_fd.flags = new_flags;
		fs().lseek(new_fd, fs().lseek(fd, 0, SEEK_CUR), SEEK_SET);
		return new_id;
	};

	auto release_new_id = [&] (Errno e) -> int { bits.free(new_id); return e; };

	if (fd.open_file_ptr) {
		return fs().open_file(fd.path.string(), new_flags).convert<int>(
			[&] (Open_file &dup_of) {
				dup_of.modified = fd.open_file_ptr->modified;
				return new_file_descriptor(dup_of);
			},
			[&] (Errno e) { return release_new_id(e); });
	}

	if (fd.open_dir_ptr)
		return fs().open_dir(fd.path.string(), new_flags).convert<int>(
			[&] (Open_dir &dup_od) { return new_file_descriptor(dup_od); },
			[&] (Errno e)          { return release_new_id(e); });

	warning("dup called for non-file/dir descriptor (", fd.path, ")");
	return release_new_id(Errno(EBADF));
}


extern "C" int dup(int libc_fd)
{
	return with_fd(libc_fd, "dup", [&] (File_descriptor &fd) -> int {
		return fds().with_alloc([&] (Fds::Bits &bits, Fds::Space &space) {
			return bits.alloc().convert<int>(
				[&] (addr_t const libc_fd)    {
					return _dup(fd, bits, space, libc_fd); },
				[&] (Fds::Bits::Error) -> int { return Errno { EMFILE }; });
		});
	});
}


extern "C" int dup2(int libc_fd, int new_libc_fd)
{
	return with_fd(libc_fd, "dup2", [&] (File_descriptor &fd) -> int {

		if (libc_fd == new_libc_fd)
			return libc_fd;

		/*
		 * Check if 'new_libc_fd' is already in use. If so, close it before
		 * allocating it again.
		 */
		if (fd_in_use(new_libc_fd))
			close(new_libc_fd);

		return fds().with_alloc([&] (Fds::Bits &bits, Fds::Space &space) {
			return bits.alloc_addr(new_libc_fd).convert<int>(
				[&] (Ok) { return _dup(fd, bits, space, new_libc_fd); },
				[&] (Fds::Bits::Error) -> int { return Errno { EMFILE }; });
		});
	});
}


extern "C" __attribute__((alias("dup2")))
int _dup2(int libc_fd, int new_libc_fd);


extern "C" int fchdir(int libc_fd)
{
	return with_fd(libc_fd, "fchdir", [&] (File_descriptor &fd) {
		return chdir(fd.path.string()); });
}


static int _fcntl(File_descriptor &fd, int cmd, long arg)
{
	switch (cmd) {
	case F_DUPFD_CLOEXEC:
	case F_DUPFD:
		{
			/*
			 * This operation is supposed to allocate the lowest fd starting at
			 * 'arg'. We take the shortcut of mirroring 'dup', which allocates
			 * the lowest available fd, starting at 0.
			 */
			static bool warned_once;
			if (!warned_once)
				warning("fcntl(F_DUPFD) not fully implemented");
			warned_once = true;

			return fds().with_alloc([&] (Fds::Bits &bits, Fds::Space &space) {
				return bits.alloc().convert<int>(
					[&] (addr_t const libc_fd)    { return _dup(fd, bits, space, libc_fd); },
					[&] (Fds::Bits::Error) -> int { return Errno { EMFILE }; });
			});
		}
	case F_GETFD:
		return fd.cloexec ? FD_CLOEXEC : 0;

	case F_SETFD:
		fd.cloexec = arg == FD_CLOEXEC;
		return 0;

	case F_GETFL: return fd.flags;
	case F_SETFL: {
			/* only the specified flags may be changed */
			long const mask = (O_NONBLOCK | O_APPEND | O_ASYNC | O_FSYNC);
			fd.flags = (fd.flags & ~mask) | (arg & mask);
		} return 0;

	/* file lock operations always succeed */
	case F_GETLK:
		((struct flock *)arg)->l_type = F_UNLCK;
		return 0;

	case F_SETLK:
	case F_SETLKW:
		return 0;

	default:
		break;
	}

	/* limit the amount of repeating error messages in the log */
	static int previous_unsupported_command = -1;
	if (cmd != previous_unsupported_command) {
		previous_unsupported_command = cmd;
		error("fcntl(): command ", Hex(cmd), " not supported - vfs");
	}
	return Errno(EINVAL);
}


__SYS_(int, fcntl, (int libc_fd, int cmd, ...),
{
	va_list ap;
	int res;
	va_start(ap, cmd);
	res = with_fd(libc_fd, "fcntl", [&] (File_descriptor &fd) {
		if (fd.socket_ptr)
			return socket_fcntl(*fd.socket_ptr, cmd, va_arg(ap, long));
		else
			return _fcntl(fd, cmd, va_arg(ap, long));
	});
	va_end(ap);
	return res;
})


__SYS_(int, fstat, (int libc_fd, struct stat *buf),
{
	if (!buf)
		return Errno { EFAULT };

	return with_fd(libc_fd, "fstat", [&] (File_descriptor &fd) -> int {
		if (fd.open_file_ptr || fd.open_dir_ptr) return fs().fstat(fd, *buf);
		return Errno { EBADF };
	});
})


__SYS_(int, fstatat, (int libc_fd, char const *path, struct stat *buf, int flags),
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	if (*path == '/') {
		if (flags & AT_SYMLINK_NOFOLLOW)
			return lstat(path, buf);
		return stat(path, buf);
	}

	Absolute_path abs_path;

	if (libc_fd == AT_FDCWD) {
		abs_path = cwd();
		abs_path.append_element(path);
	} else {
		int ret = with_fd(libc_fd, "fstatat", [&] (File_descriptor &fd) {
			abs_path.import(path, fd.path.string());
			return 0;
		});
		if (ret < 0)
			return ret;
	}

	return (flags & AT_SYMLINK_NOFOLLOW)
		? lstat(abs_path.base(), buf)
		:  stat(abs_path.base(), buf);
})


__SYS_(int, fstatfs, (int libc_fd, struct statfs *buf),
{
	if (!buf)
		return Errno(EFAULT);

	return with_fd(libc_fd, "fstatfs", [&] (File_descriptor &) {
		Genode::bzero(buf, sizeof(*buf));
		buf->f_flags = MNT_UNION;
		return 0;
	});
})


__SYS_(int, fsync, (int libc_fd),
{
	return with_open_file(libc_fd, "fsync", [&] (Open_file &of) {
		fs().fsync(of);
		return 0;
	});
})


__SYS_(int, fdatasync, (int libc_fd), { return fsync(libc_fd); })


__SYS_(int, ftruncate, (int libc_fd, ::off_t length),
{
	return with_open_file(libc_fd, "ftruncate", [&] (Open_file &of) {
		return fs().ftruncate(of, length); });
})


__SYS_(ssize_t, getdirentries, (int libc_fd, char *buf, size_t nbytes, ::off_t *basep),
{
	return with_open_dir(libc_fd, "getdirentries", [&] (Open_dir &od) {
		return fs().getdirentries(od, buf, nbytes, basep); });
})


__SYS_(int, ioctl, (int libc_fd, unsigned long request, char *argp),
{
	return with_fd(libc_fd, "ioctl", [&] (File_descriptor &fd) -> int {
		if (fd.socket_ptr)
			return socket_ioctl(*fd.socket_ptr, request, argp);
		else if (fd.open_file_ptr)
			return fs().ioctl(fd, request, argp);
		return Errno { EBADF };
	});
})


__SYS_(::off_t, lseek, (int libc_fd, ::off_t offset, int whence),
{
	return with_fd(libc_fd, "lseek", [&] (File_descriptor &fd) -> ::off_t {
		if (fd.open_file_ptr || fd.open_dir_ptr)
			return fs().lseek(fd, offset, whence);
		return Errno { EBADF };
	});
})


extern "C" int lstat(const char *path, struct stat *buf)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	resolved_path.remove_trailing('/');

	return fs().stat(resolved_path.base(), *buf);
}


extern "C" int mkdir(const char *path, mode_t mode)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	resolved_path.remove_trailing('/');

	return fs().mkdir(resolved_path.base(), mode);
}


__SYS_(void *, mmap, (void *addr, ::size_t length,
                      int prot, int flags,
                      int libc_fd, ::off_t offset),
{
	/* handle requests for anonymous memory */
	if ((flags & MAP_ANONYMOUS) || (flags & MAP_ANON)) {

		if (flags & MAP_FIXED) {
			Genode::error("mmap for fixed predefined address not supported yet");
			errno = EINVAL;
			return MAP_FAILED;
		}

		bool const executable = prot & PROT_EXEC;
		void *start = mem_alloc(executable)->alloc(length, config().mmap_align);
		if (!start) {
			errno = ENOMEM;
			return MAP_FAILED;
		}
		::bzero(start, align_addr(length, AT_PAGE));
		mmap_registry().insert({ .start     = start,
		                         .num_bytes = length,
		                         .anonymous = true });
		return start;
	}

	void *result = MAP_FAILED;
	(void)with_fd(libc_fd, "mmap", [&] (File_descriptor &fd) {
		result = fs().mmap(fd, addr, length, prot, flags, offset);
		return 0;
	});

	if (result != MAP_FAILED)
		mmap_registry().insert({ .start     = result,
		                         .num_bytes = length,
		                         .anonymous = false });

	return result;
})


extern "C" int munmap(void *start, ::size_t length)
{
	return mmap_registry().with_registered(start,
		/*
		 * 'attr' is a copy instead of a reference because the original gets
		 * deleted by `remove_fn()'.
		 */
		[&] (Mmap_registry::Attr const attr, auto const &remove_fn) {

			/*
			 * Remove registry entry before unmapping to avoid double insertion
			 * error if another thread gets the same start address immediately
			 * after unmapping.
			 */
			remove_fn();

			if (attr.anonymous) {
				bool const executable = true;
				mem_alloc(!executable)->free(start);
				mem_alloc(executable)->free(start);
				return 0;
			} else {
				return fs().munmap(start, length);
			}
		},
		[&] {
			warning("munmap: failed to lookup mmap region for address ", start);
			errno = EINVAL;
			return -1;
		});
}


__SYS_(int, msync, (void *start, ::size_t len, int flags),
{
	warning("libc: msync called, not implemented");
	return -1;
})


static int _open(Libc::Fds::Bits &bits, Libc::Fds::Space &space,
                 Libc::File_descriptor::Path const &path, int libc_fd, int flags)
{
	if (fs()._root_dir.directory_exists(path.string())) {

		if ((flags & O_ACCMODE) != O_RDONLY)
			return Errno { EISDIR };

		return fs().open_dir(path.string(), flags).convert<int>(
			[&] (Open_dir &od) {
				File_descriptor &new_fd = *new (fs()._kernel_heap)
					File_descriptor(space, libc_fd, od, path);

				new_fd.flags = flags;
				return libc_fd;
			},
			[&] (Errno e) -> int { return e; });
	}

	if (flags & O_DIRECTORY)
		return Errno { EISDIR };

	auto create_and_init_file_fd = [&] (Open_file &of)
	{
		if (flags & O_TRUNC) fs().ftruncate(of, 0);

		File_descriptor &new_fd = *new (fs()._kernel_heap)
			File_descriptor(space, libc_fd, of, path);

		new_fd.flags = flags;

		if (flags & O_APPEND)
			fs().lseek(new_fd, 0, SEEK_END);

		return libc_fd;
	};

	if (flags & O_CREAT) {
		int const ret = fs().create_file(path.string(), flags).convert<int>(
			[&] (Open_file &of)  { return create_and_init_file_fd(of); },
			[&] (Errno e) -> int { return e; });

		if (ret == 0 || errno != EEXIST)
			return ret;
	}

	return fs().open_file(path.string(), flags).convert<int>(
		[&] (Open_file &of)  { return create_and_init_file_fd(of); },
		[&] (Errno e) -> int { return e; });
}


__SYS_(int, open, (const char *pathname, int flags, ...),
{
	if (!pathname)
		return Errno(EFAULT);

	if (pathname[0] == '\0')
		return Errno(ENOENT);

	Absolute_path next_iteration_working_path;
	Absolute_path resolved_path;

	try {
		next_iteration_working_path.import(pathname, cwd().string());
	} catch (Absolute_path::Path_too_long) {
		return Errno(ENAMETOOLONG);
	}

	enum { FOLLOW_LIMIT = 10 };
	int follow_count = 0;
	bool leaf_symlink_resolved_in_this_iteration = false;

	do {

		Absolute_path current_iteration_working_path;
		Absolute_path partially_resolved_path;

		if (follow_count++ == FOLLOW_LIMIT)
			return Errno(ELOOP);

		leaf_symlink_resolved_in_this_iteration = false;

		current_iteration_working_path = next_iteration_working_path;

		if (resolve_symlinks_except_last_element(current_iteration_working_path.base(),
		                                         partially_resolved_path).failed())
			return -1;

		/* determine type of last element */

		struct stat stat_buf;
		int res = fs().stat(partially_resolved_path.base(), stat_buf);

		if (res == 0) {

			if (S_ISLNK(stat_buf.st_mode)) {

				if (flags & O_NOFOLLOW)
					return Errno(ELOOP);

				/* resolve last element and start over */

				if (_resolve_symlink(partially_resolved_path,
				                     next_iteration_working_path).failed())
					return -1;

				leaf_symlink_resolved_in_this_iteration = true;

			} else {
				resolved_path = partially_resolved_path;
				break;
			}

		} else {

			/* stat() failed */

			if (errno == ENOENT) {
				if (!(flags & O_CREAT))
					return -1;
				else
					resolved_path = partially_resolved_path;
			} else
				return -1;
		}

	} while (leaf_symlink_resolved_in_this_iteration);

	File_descriptor::Path const path { resolved_path.string() };

	return fds().with_alloc([&] (Fds::Bits &bits, Fds::Space &space) {
		return bits.alloc().convert<int>(
			[&] (addr_t const libc_fd) {
				int ret = _open(bits, space, path, libc_fd, flags);
				if (ret < 0) bits.free(libc_fd);
				return ret;
			},
			[&] (Fds::Bits::Error) -> int { return Errno { EMFILE }; });
		});
})


__SYS_(int, openat, (int libc_fd, const char *path, int flags, ...),
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	va_list ap;
	va_start(ap, flags);
	mode_t mode = va_arg(ap, unsigned);
	va_end(ap);


	if (*path == '/') {
		return open(path, flags, mode);
	}

	Absolute_path abs_path;

	if (libc_fd == AT_FDCWD) {
		abs_path = cwd();
		abs_path.append_element(path);
	} else {
		int ret = with_fd(libc_fd, "openat", [&] (File_descriptor &fd) {
			abs_path.import(path, fd.path.string());
			return 0;
		});
		if (ret < 0)
			return ret;
	}

	return open(abs_path.base(), flags, mode);
})


extern "C" int pipe(int pipefd[2]) {
	return pipe2(pipefd, 0); }


extern "C" int pipe2(int pipefd[2], int flags)
{
	Absolute_path base_path(config().pipe);
	if (base_path == "") {
		error(__func__, ": pipe fs not mounted");
		return Errno(EACCES);
	}

	int meta_libc_fd = 0;
	{
		Absolute_path new_path = base_path;
		new_path.append("/new");

		meta_libc_fd = open(new_path.base(), O_RDONLY);
		if (meta_libc_fd < 0) {
			Genode::error("failed to create pipe at ", new_path);
			return Errno(EACCES);
		}

		char buf[32] { };
		int const n = read(meta_libc_fd, buf, sizeof(buf)-1);
		if (n < 1) {
			error("failed to read pipe at ", new_path);
			close(meta_libc_fd);
			return Errno(EACCES);
		}
		buf[n] = '\0';
		base_path.append("/");
		base_path.append(buf);
	}

	auto open_pipe_fd = [&] (auto path_suffix, auto flags)
	{
		Absolute_path path = base_path;
		path.append(path_suffix);

		return open(path.base(), flags);
	};

	pipefd[0] = open_pipe_fd("/out", O_RDONLY);
	pipefd[1] = open_pipe_fd("/in",  O_WRONLY);

	close(meta_libc_fd);

	if (pipefd[0] < 0 || pipefd[1] < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return Errno(EACCES);
	}

	if (flags & O_NONBLOCK) {
		int err = fcntl(pipefd[0], F_SETFL, O_NONBLOCK)
		        | fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
		if (err != 0)
			warning("pipe plugin does not support O_NONBLOCK");
	}
	return 0;
}


__SYS_(ssize_t, read, (int libc_fd, void *buf, ::size_t count),
{
	return with_fd(libc_fd, "read", [&] (File_descriptor &fd) -> ssize_t {

		if (fd.flags & O_DIRECTORY)
			return Errno(EISDIR);

		if ((fd.flags & O_ACCMODE) == O_WRONLY)
			return Errno(EBADF);

		if (fd.open_file_ptr)
			return fs().read(fd, buf, count);

		if (fd.socket_ptr)
			return socket_read(*fd.socket_ptr, buf, count);

		warning("read from neither file or socket");
		return Errno(EBADF);
	});
})


extern "C" ssize_t readlink(const char *path, char *buf, ::size_t bufsiz)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	return fs().readlink(resolved_path.base(), buf, bufsiz);
}


extern "C" int rename(const char *oldpath, const char *newpath)
{
	if (!oldpath || !newpath)
		return Errno(EFAULT);

	if ((oldpath[0] == '\0') || (newpath[0] == '\0'))
		return Errno(ENOENT);

	Absolute_path resolved_oldpath, resolved_newpath;

	if (resolve_symlinks_except_last_element(oldpath, resolved_oldpath).failed())
		return -1;

	if (resolve_symlinks_except_last_element(newpath, resolved_newpath).failed())
		return -1;

	resolved_oldpath.remove_trailing('/');
	resolved_newpath.remove_trailing('/');

	return fs().rename(resolved_oldpath.base(), resolved_newpath.base());
}


extern "C" int rmdir(const char *path)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	resolved_path.remove_trailing('/');

	struct stat stat_buf { };

	if (stat(resolved_path.base(), &stat_buf) == -1)
		return -1;

	if (!S_ISDIR(stat_buf.st_mode)) {
		errno = ENOTDIR;
		return -1;
	}

	return fs().unlink(resolved_path.base());
}


extern "C" int stat(const char *path, struct stat *buf)
{
	if (!path || !buf)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks(path, resolved_path).failed())
		return -1;

	resolved_path.remove_trailing('/');

	return fs().stat(resolved_path.base(), *buf);
}


extern "C" int symlink(const char *oldpath, const char *newpath)
{
	if (!oldpath || !newpath)
		return Errno(EFAULT);

	if ((oldpath[0] == '\0') || (newpath[0] == '\0'))
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(newpath, resolved_path).failed())
		return -1;

	return fs().symlink(oldpath, resolved_path.base());
}


extern "C" int unlink(const char *path)
{
	if (!path)
		return Errno(EFAULT);

	if (path[0] == '\0')
		return Errno(ENOENT);

	Absolute_path resolved_path;

	if (resolve_symlinks_except_last_element(path, resolved_path).failed())
		return -1;

	return fs().unlink(resolved_path.base());
}


__SYS_(ssize_t, write, (int libc_fd, const void *buf, ::size_t count),
{
	int flags = fcntl(libc_fd, F_GETFL);

	if ((flags != -1) && (flags & O_APPEND))
		lseek(libc_fd, 0, SEEK_END);

	return with_fd(libc_fd, "write", [&] (File_descriptor &fd) {
		if (fd.socket_ptr)
			return socket_write(*fd.socket_ptr, buf, count);
		else
			return fs().write(fd, buf, count);
	});
})


extern "C" int __getcwd(char *dst, ::size_t dst_size)
{
	copy_cstring(dst, cwd().string(), dst_size);
	return 0;
}


static int with_aio_fd(auto /* const or non-const */ *iocb_ptr, auto const &fn)
{
	if (!iocb_ptr)
		return Errno(EINVAL);

	return with_fd(iocb_ptr->aio_fildes, "aio", [&] (File_descriptor &fd) -> int {
		if (fd.lio_list_queued >= File_descriptor::MAX_AIOCB_PER_FD)
			return Errno(EAGAIN);

		return fn(fd);
	});
}


extern "C" int aio_fsync(int op, struct aiocb *iocb)
{
	/* not supported for now */
	return Errno(EINVAL);
}


extern "C" int aio_cancel(int fildes, struct aiocb *iocb)
{
	/* not supported for now */
	return Errno(EBADF);
}


extern "C" ssize_t aio_return(struct aiocb *iocb)
{
	return with_aio_fd(iocb, [&] (File_descriptor &fd) -> ssize_t {

		int error = EINVAL;
		ssize_t result = 0;
		File_descriptor::apply_lio(fd, iocb, [&] (File_descriptor::Aio_job &aio_job) {
			using State = File_descriptor::Aio_job::State;

			switch (aio_job.state) {
			case State::COMPLETE:
				error  = aio_job.error;
				result = aio_job.result;
				break;
			case State::FREE:        error = EINVAL; break;
			case State::IN_PROGRESS: error = EINVAL; break;
			case State::PENDING:     error = EINVAL; break;
			}

			aio_job.free();

			--fd.lio_list_completed;
		});

		return !error ? result : Errno(error);
	});
}


extern "C" int aio_error(const struct aiocb *iocb)
{
	return with_aio_fd(iocb, [&] (File_descriptor const &fd) -> int {

		int error = EINVAL;
		File_descriptor::apply_lio(fd, iocb, [&] (File_descriptor::Aio_job const &aio_job) {
			using State = File_descriptor::Aio_job::State;

			switch (aio_job.state) {
			case State::COMPLETE:    error = aio_job.error;  break;
			case State::FREE:        error = EINVAL;      break;
			case State::IN_PROGRESS: error = EINPROGRESS; break;
			case State::PENDING:     error = EINPROGRESS; break;
			}
		});

		if (!error)
			return 0;

		return Errno(error);
	});
}


extern "C" int aio_suspend(const struct aiocb * const iocbs[], int niocb,
                           const struct timespec *timeout)
{
	int const timeout_ms =
		timeout ? (timeout->tv_sec * 1000 + timeout->tv_nsec * 1'000'000)
		        : 0;

	bool at_least_one_okay = false;
	int result = -1;
	for (int i = 0; i < niocb; i++) {
		if (iocbs[i] == NULL)
			continue;

		int const libc_fd = iocbs[i]->aio_fildes;

		result = with_fd(libc_fd, "aio_suspend", [&] (File_descriptor &fd) {
			return fs().wait_aio(fd, timeout_ms); });

		at_least_one_okay |= result == 0;
		if (result != 0)
			break;
	}

	return at_least_one_okay ? 0 : result;
}


extern "C" int aio_read(struct aiocb *iocb)
{
	return with_aio_fd(iocb, [&] (File_descriptor &fd) -> int {
		iocb->aio_lio_opcode = LIO_READ;
		return fs().enqueue_aiocb(fd, *iocb);
	});
}


extern "C" int aio_write(struct aiocb *iocb)
{
	return with_aio_fd(iocb, [&] (File_descriptor &fd) -> int {
		iocb->aio_lio_opcode = LIO_WRITE;
		return fs().enqueue_aiocb(fd, *iocb);
	});
}


extern "C" int lio_listio(int mode, struct aiocb * const list[], int nent,
                          struct sigevent *sig)
{
	if (sig != NULL)
		return Errno(EINVAL);

	if (mode != LIO_NOWAIT)
		return Errno(EINVAL);

	int result = 0;

	for (int i = 0; i < nent; i++) {
		if (list[i] == NULL)
			continue;

		struct aiocb *iocb = list[i];

		result = with_aio_fd(iocb, [&] (File_descriptor &fd) -> int {
			return fs().enqueue_aiocb(fd, *iocb); });

		if (result != 0)
			break;
	}
	return result;
}
