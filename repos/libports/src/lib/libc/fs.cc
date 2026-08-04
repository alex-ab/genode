/*
 * \brief  Libc access to component-local virtual file system
 * \author Norman Feske
 * \author Christian Helmuth
 * \date   2026-06-l9
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/env.h>
#include <base/log.h>
#include <vfs/dir_file_system.h>
#include <net/mac_address.h>

/* libc includes */
extern "C" {
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <sys/soundcard.h>
#include <dlfcn.h>
#include <net/if.h>
#include <net/if_tap.h>
#include <assert.h>
#include <aio.h>
}

/* libc-internal includes */
#include <internal/kernel.h>
#include <internal/fs.h>
#include <internal/mem_alloc.h>
#include <internal/errno.h>
#include <internal/monitor.h>
#include <internal/current_time.h>

namespace { using Fn = Libc::Monitor::Function_result; }


static ino_t pseudo_inode_from_path(char const *path)
{
	using namespace Genode;

	uint64_t checksum = 0;
	for (uint8_t *s = (uint8_t *)path; *s; s++) {
		checksum ^= *s;

		/* xorshift64 */
		uint64_t x = checksum;
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		checksum = x;
	}
	return ino_t(checksum);
};


/**
 * Utility to convert VFS stat struct to the libc stat struct
 *
 * Code shared between 'stat' and 'fstat'.
 */
static void vfs_stat_to_libc_stat_struct(Genode::Vfs::Directory_service::Stat const &src,
                                         char const *path, struct stat &dst)
{
	using namespace Genode;

	enum { FS_BLOCK_SIZE = 4096 * 16 };

	unsigned const readable_bits   = S_IRUSR,
	               writeable_bits  = S_IWUSR,
	               executable_bits = S_IXUSR;

	auto type = [] (Genode::Vfs::Node_type type)
	{
		switch (type) {
		case Vfs::Node_type::DIRECTORY:          return S_IFDIR;
		case Vfs::Node_type::CONTINUOUS_FILE:    return S_IFREG;
		case Vfs::Node_type::TRANSACTIONAL_FILE: return S_IFCHR;
		case Vfs::Node_type::SYMLINK:            return S_IFLNK;
		}
		return 0;
	};

	dst = { };

	timespec const mtime {
		.tv_sec  = time_t( src.modification_time.ms_since_1970 / 1000),
		.tv_nsec = time_t((src.modification_time.ms_since_1970 % 1000)*1000*1000) };

	dst.st_uid     = 0;
	dst.st_gid     = 0;
	dst.st_mode    = (src.rwx.readable   ? readable_bits   : 0)
	               | (src.rwx.writeable  ? writeable_bits  : 0)
	               | (src.rwx.executable ? executable_bits : 0)
	               | type(src.type);
	dst.st_size    = src.size;
	dst.st_blksize = FS_BLOCK_SIZE;
	dst.st_blocks  = (dst.st_size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
	dst.st_ino     = pseudo_inode_from_path(path);
	dst.st_dev     = src.device;
	dst.st_mtim    = mtime;
	dst.st_nlink   = 1;
}


namespace Libc {

	bool read_ready_from_kernel(File_descriptor &fd)
	{
		if (!fd.open_file_ptr)
			return false;

		Vfs::Vfs_handle &handle = fd.open_file_ptr->handle;
		handle.notify_read_ready();
		return handle.read_ready();
	}

	void notify_read_ready_from_kernel(File_descriptor &fd)
	{
		if (fd.open_file_ptr) {
			Vfs::Vfs_handle &handle = fd.open_file_ptr->handle;
			handle.notify_read_ready();
		}
	}

	bool write_ready_from_kernel(File_descriptor &fd)
	{
		if (!fd.open_file_ptr)
			return false;

		Vfs::Vfs_handle &handle = fd.open_file_ptr->handle;
		return handle.write_ready();
	}
}


namespace Libc { struct Cached_ioctl_info; }


/*
 * Cache the latest info file to accomodate highly frequent 'ioctl'
 * calls as observed by the OSS plugin.
 */
struct Libc::Cached_ioctl_info : Noncopyable
{
	Constructible<Readonly_file> _file { };

	using Path = File_descriptor::Path;
	Path _path { };

	Cached_ioctl_info() { };

	template <typename FN>
	void with_file(Fs &fs, Path const &path, FN const &fn)
	{
		if (path != _path && fs._root_dir.file_exists(path.string())) {
			_file.construct(fs._root_dir, path);
			_path = path;
		}

		if (path == _path && _file.constructed())
			fn(*_file);
	}
};


static Libc::Cached_ioctl_info &cached_ioctl_info()
{
	static Libc::Cached_ioctl_info inst { };
	return inst;
}


/*
 * This function must be called in entrypoint context only.
 */
static void with_info(Libc::Fs &fs, Libc::File_descriptor &fd, auto const &fn)
{
	using namespace Libc;

	using Absolute_path = Genode::Path<PATH_MAX>;
	Absolute_path path = fd.ioctl_dir();
	path.append_element("info");

	cached_ioctl_info().with_file(fs, path, [&] (Readonly_file const &file) {

		char buffer[4096] { };

		Byte_range_ptr range(buffer,
		                     min((size_t)(fs._root_dir.file_size(path.string())),
		                         sizeof(buffer)));

		with_node_file_content(file, range, [&] (Node const &node) { fn(node); });
	});
}


int Libc::Fs::access(char const *path, int amode)
{
	bool succeeded = false;
	_monitor.monitor([&] {
		if (_vfs.dir_entry_exists(path))
			succeeded = true;
		return Fn::COMPLETE;
	});
	if (succeeded)
		return 0;

	errno = ENOENT;
	return -1;
}


Libc::Fs::Open_file_result Libc::Fs::open_file_from_kernel(const char *path, int flags)
{
	if (flags & O_DIRECTORY)
		return Errno { ENOTDIR };

	Vfs::Vfs_handle *handle_ptr = 0;

	using Result = Vfs::Directory_service::Open_result;

	switch (_vfs.open(path, flags, &handle_ptr, _kernel_heap)) {
	case Result::OPEN_OK:                break;
	case Result::OPEN_ERR_UNACCESSIBLE:  return Errno { ENOENT };
	case Result::OPEN_ERR_NO_PERM:       return Errno { EPERM  };
	case Result::OPEN_ERR_EXISTS:        return Errno { EEXIST };
	case Result::OPEN_ERR_NAME_TOO_LONG: return Errno { ENAMETOOLONG };
	case Result::OPEN_ERR_NO_SPACE:      return Errno { ENOSPC };
	case Result::OPEN_ERR_OUT_OF_RAM:    return Errno { ENOSPC };
	case Result::OPEN_ERR_OUT_OF_CAPS:   return Errno { ENOSPC };
	}

	handle_ptr->handler(&_response_handler);

	return *new (_kernel_heap) Open_file(*handle_ptr);
}


Libc::Fs::Open_file_result Libc::Fs::open_file(char const *path, int flags)
{
	int result_errno = 0;
	Vfs::Vfs_handle *handle_ptr = nullptr;

	_monitor.monitor([&] {

		using Result = Vfs::Directory_service::Open_result;

		switch (_vfs.open(path, flags, &handle_ptr, _kernel_heap)) {
		case Result::OPEN_OK:                break;
		case Result::OPEN_ERR_UNACCESSIBLE:  result_errno = ENOENT;       return Fn::COMPLETE;
		case Result::OPEN_ERR_NO_PERM:       result_errno = EPERM;        return Fn::COMPLETE;
		case Result::OPEN_ERR_EXISTS:        result_errno = EEXIST;       return Fn::COMPLETE;
		case Result::OPEN_ERR_NAME_TOO_LONG: result_errno = ENAMETOOLONG; return Fn::COMPLETE;
		case Result::OPEN_ERR_NO_SPACE:      result_errno = ENOSPC;       return Fn::COMPLETE;
		case Result::OPEN_ERR_OUT_OF_RAM:    result_errno = ENOSPC;       return Fn::COMPLETE;
		case Result::OPEN_ERR_OUT_OF_CAPS:   result_errno = ENOSPC;       return Fn::COMPLETE;
		}
		return Fn::COMPLETE;
	});

	if (!handle_ptr)
		return Errno { result_errno };

	handle_ptr->handler(&_response_handler);

	return *new (_kernel_heap) Open_file(*handle_ptr);
}


Libc::Fs::Open_file_result Libc::Fs::create_file(char const *path, int flags)
{
	int result_errno = 0;
	Vfs::Vfs_handle *handle_ptr = nullptr;

	_monitor.monitor([&] {

		using Result = Vfs::Directory_service::Open_result;

		switch (_vfs.open(path, flags | O_EXCL | O_CREAT, &handle_ptr, _kernel_heap)) {
		case Result::OPEN_OK: break;
		case Result::OPEN_ERR_EXISTS:

			/* file has been created by someone else in the meantime */
			if (flags & O_NOFOLLOW) {
				result_errno = ELOOP;
				return Fn::COMPLETE;
			}
			result_errno = EEXIST;
			return Fn::COMPLETE;

		case Result::OPEN_ERR_NO_PERM:       result_errno = EPERM;        return Fn::COMPLETE;
		case Result::OPEN_ERR_UNACCESSIBLE:  result_errno = ENOENT;       return Fn::COMPLETE;
		case Result::OPEN_ERR_NAME_TOO_LONG: result_errno = ENAMETOOLONG; return Fn::COMPLETE;
		case Result::OPEN_ERR_NO_SPACE:      result_errno = ENOSPC;       return Fn::COMPLETE;
		case Result::OPEN_ERR_OUT_OF_RAM:    result_errno = ENOSPC;       return Fn::COMPLETE;
		case Result::OPEN_ERR_OUT_OF_CAPS:   result_errno = ENOSPC;       return Fn::COMPLETE;
		}

		return Fn::COMPLETE;
	});

	if (!handle_ptr)
		return Errno { result_errno };

	handle_ptr->handler(&_response_handler);

	return *new (_kernel_heap) Open_file(*handle_ptr);
}


Libc::Fs::Open_dir_result Libc::Fs::open_dir(char const *path, int flags)
{
	int result_errno = 0;
	Vfs::Vfs_handle *handle_ptr = nullptr;

	_monitor.monitor([&] {

		using Result = Vfs::Directory_service::Opendir_result;

		switch (_vfs.opendir(path, false, &handle_ptr, _kernel_heap)) {
		case Result::OPENDIR_OK:                      break;
		case Result::OPENDIR_ERR_LOOKUP_FAILED:       result_errno = ENOENT;       return Fn::COMPLETE;
		case Result::OPENDIR_ERR_NAME_TOO_LONG:       result_errno = ENAMETOOLONG; return Fn::COMPLETE;
		case Result::OPENDIR_ERR_NODE_ALREADY_EXISTS: result_errno = EEXIST;       return Fn::COMPLETE;
		case Result::OPENDIR_ERR_NO_SPACE:            result_errno = ENOSPC;       return Fn::COMPLETE;
		case Result::OPENDIR_ERR_OUT_OF_RAM:
		case Result::OPENDIR_ERR_OUT_OF_CAPS:
		case Result::OPENDIR_ERR_PERMISSION_DENIED:   result_errno = EPERM;        return Fn::COMPLETE;
		}

		/* the directory was successfully opened */
		return Fn::COMPLETE;
	});

	if (!handle_ptr)
		return Errno { result_errno };

	return *new (_kernel_heap) Open_dir { *handle_ptr, path };
}


void Libc::Fs::destroy(Open_file &of)
{
	fsync(of);

	/* cancel and sync with blocking read */
	_monitor.monitor([&] {
		of.closing = true;
		if (of.blocking) return Fn::INCOMPLETE;

		of.handle.close();
		return Fn::COMPLETE;
	});
	Genode::destroy(_kernel_heap, &of);
}


void Libc::Fs::destroy(Open_dir &od)
{
	_monitor.monitor([&] {
		od.handle.close();
		return Fn::COMPLETE;
	});
	Genode::destroy(_kernel_heap, &od);
}


struct Sync
{
	enum { INITIAL, TIMESTAMP_UPDATED, QUEUED, COMPLETE } state { INITIAL };

	Genode::Vfs::Vfs_handle &vfs_handle;
	Genode::Vfs::Timestamp   mtime { };

	struct Attr { bool update_mtime; };

	Sync(Genode::Vfs::Vfs_handle &vfs_handle, Attr const attr,
	     Libc::Current_real_time &current_real_time)
	:
		vfs_handle(vfs_handle)
	{
		if (!attr.update_mtime || !current_real_time.has_real_time()) {

			state = TIMESTAMP_UPDATED;

		} else {
			timespec const ts = current_real_time.current_real_time();

			mtime.ms_since_1970 = ts.tv_sec >= 0
			                    ? ts.tv_sec*1000ull + ts.tv_nsec/1000000ull
			                    : 0;
		}
	}

	bool complete()
	{
		switch (state) {
		case Sync::INITIAL:
			if (!vfs_handle.update_modification_timestamp(mtime))
				return false;
			state = Sync::TIMESTAMP_UPDATED; [[ fallthrough ]];
		case Sync::TIMESTAMP_UPDATED:
			if (!vfs_handle.queue_sync())
				return false;
			state = Sync::QUEUED; [[ fallthrough ]];
		case Sync::QUEUED:
			if (vfs_handle.complete_sync() == Genode::Vfs::SYNC_QUEUED)
				return false;
			state = Sync::COMPLETE; [[ fallthrough ]];
		case Sync::COMPLETE:
			break;
		}
		return true;
	}
};


int Libc::Fs::fstat(File_descriptor &fd, struct stat &buf)
{
	if (fd.open_file_ptr && fd.open_file_ptr->modified)
		fsync(*fd.open_file_ptr);

	int const result = stat(fd.path.string(), buf);

	/*
	 * The libc expects stdout to be a character device.
	 * If 'st_mode' is set to 'S_IFREG', 'printf' does not work.
	 */
	if (fd.libc_fd == 1) {
		buf.st_mode &= ~S_IFMT;
		buf.st_mode |=  S_IFCHR;
	}

	return result;
}


int Libc::Fs::mkdir(const char *path, mode_t mode)
{
	Vfs::Vfs_handle *dir_handle_ptr = nullptr;

	int result = -1;
	int result_errno = 0;
	_monitor.monitor([&] {
		using Result = Vfs::Directory_service::Opendir_result;
		switch (_vfs.opendir(path, true, &dir_handle_ptr, _kernel_heap)) {
		case Result::OPENDIR_ERR_LOOKUP_FAILED:       result_errno = ENOENT;       break;
		case Result::OPENDIR_ERR_NAME_TOO_LONG:       result_errno = ENAMETOOLONG; break;
		case Result::OPENDIR_ERR_NODE_ALREADY_EXISTS: result_errno = EEXIST;       break;
		case Result::OPENDIR_ERR_NO_SPACE:            result_errno = ENOSPC;       break;
		case Result::OPENDIR_ERR_OUT_OF_RAM:          result_errno = EPERM;        break;
		case Result::OPENDIR_ERR_OUT_OF_CAPS:         result_errno = EPERM;        break;
		case Result::OPENDIR_ERR_PERMISSION_DENIED:   result_errno = EPERM;        break;
		case Result::OPENDIR_OK:
			dir_handle_ptr->close();
			result = 0;
			break;
		}

		return Fn::COMPLETE;
	});

	if (result == -1)
		errno = result_errno;

	return result;
}


int Libc::Fs::stat_from_kernel(const char *path, struct stat &buf)
{
	if (!path)
		return Errno(EFAULT);

	using Result = Vfs::Directory_service::Stat_result;

	Vfs::Directory_service::Stat stat;

	switch (_vfs.stat(path, stat)) {
	case Result::STAT_ERR_NO_ENTRY: errno = ENOENT; return -1;
	case Result::STAT_ERR_NO_PERM:  errno = EACCES; return -1;
	case Result::STAT_OK:                           break;
	}

	vfs_stat_to_libc_stat_struct(stat, path, buf);
	return 0;
}


int Libc::Fs::stat(char const *path, struct stat &buf)
{
	if (!path)
		return Errno(EFAULT);

	using Result = Vfs::Directory_service::Stat_result;

	Vfs::Directory_service::Stat stat;

	int result = -1;
	int result_errno = 0;
	_monitor.monitor([&] {
		switch (_vfs.stat(path, stat)) {
		case Result::STAT_ERR_NO_ENTRY: result_errno = ENOENT; break;
		case Result::STAT_ERR_NO_PERM:  result_errno = EACCES; break;
		case Result::STAT_OK:
			vfs_stat_to_libc_stat_struct(stat, path, buf);
			result = 0;
			break;
		}

		return Fn::COMPLETE;
	});

	if (result == -1)
		errno = result_errno;

	return result;
}


ssize_t Libc::Fs::write(File_descriptor &fd, const void *buf, ::size_t count)
{
	using Result = Vfs::Write_result;

	if (!fd.open_file_ptr || (fd.flags & O_ACCMODE) == O_RDONLY)
		return Errno(EBADF);

	Open_file &of = *fd.open_file_ptr;

	::size_t out_count  = 0;
	Result   out_result = Result::WRITE_OK;

	Const_byte_range_ptr const src { (char const *)buf, count };

	if (fd.flags & O_NONBLOCK) {
		_monitor.monitor([&] {
			out_result = of.handle.write(src, out_count);
			return Fn::COMPLETE;
		});
	} else {
		Vfs::file_size const initial_seek { of.handle.seek() };

		void const *_buf        { buf };
		::size_t    _count      { count };
		::size_t   &_out_count  { out_count };
		Result     &_out_result { out_result };
		::off_t     _offset     { 0 };
		unsigned    _iteration  { 0 };

		auto _fd_refers_to_continuous_file = [&]
		{
			using Result = Vfs::Directory_service::Stat_result;

			Vfs::Directory_service::Stat stat { };

			if (_vfs.stat(fd.path.string(), stat) != Result::STAT_OK)
				return false;

			return stat.type == Vfs::Node_type::CONTINUOUS_FILE;
		};

		_monitor.monitor([&]
		{
			for (;;) {

				/* number of bytes written in one iteration */
				::size_t partial_out_count = 0;

				Const_byte_range_ptr const src { (char const *)_buf + _offset,
				                                  _count };

				_out_result = of.handle.write(src, partial_out_count);

				if (_out_result == Result::WRITE_ERR_WOULD_BLOCK)
					return Fn::INCOMPLETE;

				if (_out_result != Result::WRITE_OK)
					return Fn::COMPLETE;

				/* increment byte count reported to caller */
				_out_count += partial_out_count;

				bool const write_complete = (partial_out_count == _count);
				if (write_complete)
					return Fn::COMPLETE;

				/*
				 * If the write has not consumed all bytes, set up
				 * another partial write iteration with the remaining
				 * bytes as 'count'.
				 *
				 * The costly 'fd_refers_to_continuous_file' is called
				 * for the first iteration only.
				 */
				bool const continuous_file = (_iteration > 0 || _fd_refers_to_continuous_file());

				if (!continuous_file) {
					warning("partial write on transactional file");
					_out_result = Result::WRITE_ERR_IO;
					return Fn::COMPLETE;
				}

				_iteration++;

				bool const stalled = (partial_out_count == 0);
				if (stalled) {
					return Fn::INCOMPLETE;
				}

				/* issue new write operation for remaining bytes */
				_count  -= partial_out_count;
				_offset += partial_out_count;
				of.handle.advance_seek(partial_out_count);
			}
		});

		/* XXX reset seek pointer after loop (will be advanced below by out_count) */
		of.handle.seek(initial_seek);
	}

	switch (out_result) {
	case Result::WRITE_ERR_WOULD_BLOCK: return Errno(EWOULDBLOCK);
	case Result::WRITE_ERR_INVALID:     return Errno(EINVAL);
	case Result::WRITE_ERR_IO:          return Errno(EIO);
	case Result::WRITE_OK:              break;
	}

	of.handle.advance_seek(out_count);
	of.modified = true;

	return out_count;
}


ssize_t Libc::Fs::read(File_descriptor &fd, void *buf, ::size_t count)
{
	using Result = Vfs::Read_result;

	if (!fd.open_file_ptr)
		return Errno { EBADF };

	Open_file &of = *fd.open_file_ptr;

	int result_errno = 0;
	size_t out_count = 0;
	bool queued = false;

	_monitor.monitor([&] {

		if (of.closing) result_errno = EBADF;

		if (!queued && fd.flags & O_NONBLOCK && !read_ready_from_kernel(fd))
			result_errno = EAGAIN;

		if (result_errno) {
			of.blocking = false;
			return Fn::COMPLETE;
		}

		of.blocking = true; /* sync with close */

		Byte_range_ptr const dst { (char *)buf, count };

		Result result = of.handle.read(dst);
		if (result == Vfs::Read_error::RETRY) {
			queued = true;
			return Fn::INCOMPLETE; /* keep blocking */
		}

		of.blocking = false;

		result.with_result(
			[&] (size_t num_bytes) {
				of.handle.advance_seek(num_bytes);
				out_count = num_bytes;
			},
			[&] (Vfs::Read_error) { result_errno = EINVAL; });

		return Fn::COMPLETE; /* success or error out */
	});

	if (result_errno)
		return Errno(result_errno);

	return out_count;
}


ssize_t Libc::Fs::getdirentries(Open_dir &od, char *buf, size_t nbytes, off_t *basep)
{
	if (nbytes < sizeof(struct dirent)) {
		error("getdirentries: buffer too small");
		return -1;
	}

	using Result = Vfs::Read_result;
	using Dirent = Vfs::Directory_service::Dirent;

	Dirent dirent_out;
	Result result = Vfs::Read_error::DENIED;

	_monitor.monitor([&] {

		Byte_range_ptr const dst { (char *)&dirent_out, sizeof(Dirent) };
		result = od.handle.read(dst);

		return result == Vfs::Read_error::RETRY ? Fn::INCOMPLETE : Fn::COMPLETE;
	});

	bool const ok = result.convert<bool>(
		[&] (size_t num_bytes) { return num_bytes >= sizeof(Dirent); },
		[&] (Vfs::Read_error) { return false; });

	using Dirent_type = Vfs::Directory_service::Dirent_type;

	if (!ok || dirent_out.type == Dirent_type::END)
		return 0;

	/*
	 * Convert dirent structure from VFS to libc
	 */

	auto dirent_type = [] (Dirent_type type)
	{
		switch (type) {
		case Dirent_type::DIRECTORY:          return DT_DIR;
		case Dirent_type::CONTINUOUS_FILE:    return DT_REG;
		case Dirent_type::TRANSACTIONAL_FILE: return DT_CHR;
		case Dirent_type::SYMLINK:            return DT_LNK;
		case Dirent_type::END:                return DT_UNKNOWN;
		}
		return DT_UNKNOWN;
	};

	dirent &dirent = *(struct dirent *)buf;
	dirent = { };

	Genode::copy_cstring(dirent.d_name, dirent_out.name.buf, sizeof(dirent.d_name));

	Open_dir::Path const entry_path { od.path, "/", Cstring(dirent.d_name) };

	dirent.d_type   = dirent_type(dirent_out.type);
	dirent.d_fileno = pseudo_inode_from_path(entry_path.string());
	dirent.d_reclen = sizeof(struct dirent);


	dirent.d_namlen = Genode::strlen(dirent.d_name);

	/*
	 * Keep track of VFS seek pointer and user-supplied basep.
	 */
	od.handle.advance_seek(sizeof(Vfs::Directory_service::Dirent));

	*basep += sizeof(struct dirent);

	return sizeof(struct dirent);
}


namespace {
	struct Ioctl_result
	{
		bool handled;
		int  error;
	};
}


static Ioctl_result ioctl_tio(Libc::Fs &fs, Libc::File_descriptor &fd,
                              unsigned long request, char *argp)
{
	using namespace Libc;

	if (!argp)
		return { true, EINVAL };

	bool handled = false;

	if (request == TIOCGWINSZ) {

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() == "terminal") {
					::winsize *winsize = (::winsize *)argp;
					winsize->ws_row = info.attribute_value("rows",    25U);
					winsize->ws_col = info.attribute_value("columns", 80U);
					handled = true;
				}
			});

			return Fn::COMPLETE;
		});

	} else if (request == TIOCGETA) {

		bool terminal = false;

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() == "terminal")
					terminal = true;
			});

			return Fn::COMPLETE;
		});

		/* handle '/dev/log' like a terminal for stdout line-buffering */

		if (terminal || (fd.path == "/dev/log")) {
			::termios *termios = (::termios *)argp;

			termios->c_iflag = 0;
			termios->c_oflag = 0;
			termios->c_cflag = 0;
			/*
			 * Set 'ECHO' flag, needed by libreadline. Otherwise, echoing
			 * user input doesn't work in bash.
			 */
			termios->c_lflag = ECHO;
			::memset(termios->c_cc, _POSIX_VDISABLE, sizeof(termios->c_cc));
			termios->c_ispeed = 0;
			termios->c_ospeed = 0;

			handled = true;
		} else
			return { true, ENOTTY };

	} else if (request == TIOCSETA) {

		/*
		 * As TIOCGETA above only returns the for now required
		 * options ignore any attempt to set them.
		 */

		handled = true;

	} else if (request == TIOCFLUSH) {

		handled = true;
	}

	return { handled, 0 };
}


static Ioctl_result ioctl_dio(Libc::Fs &fs, Libc::File_descriptor &fd,
                              unsigned long request, char *argp)
{
	using namespace Libc;

	if (!argp)
		return { true, EINVAL };

	bool handled = false;

	using ::off_t;

	if (request == DIOCGMEDIASIZE) {

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() == "block") {

					size_t const size =
						info.attribute_value("size", 0UL);
					if (!size) {
						warning("block size is 0");
					}

					uint64_t const count =
						info.attribute_value("count", 0ULL);
					if (!count) {
						warning("block count is 0");
					}

					off_t disk_size = (off_t) count * size;
					if (disk_size < 0) {
						warning("disk size overflow - returning 0");
						disk_size = 0;
					}

					*(off_t*)argp = disk_size;
					handled = true;
				}
			});

			return Fn::COMPLETE;
		});

	}

	return { handled, 0 };
}


static Ioctl_result ioctl_sndctl(Libc::Fs &fs, Libc::File_descriptor &fd,
                                 unsigned long request, char *argp)
{
	using namespace Libc;

	using Absolute_path = Genode::Path<PATH_MAX>;

	bool handled = false;
	/*
	 * Initialize to "success" and any ioctl is required to set
	 * in case of error.
	 *
	 * This method will either return handled equals true if the I/O control
	 * was handled successfully or failed and the result is not successful
	 * (see the end of this method).
	 * 
	 */
	int result = 0;

	if (request == OSS_GETVERSION) {

		if (!argp) return { true, EINVAL };

		*(int *)argp = SOUND_VERSION;

		handled = true;

	} else if (request == SNDCTL_DSP_CHANNELS) {

		if (!argp) return { true, EINVAL };

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() != "oss") {
					return;
				}

				unsigned int const avail_chans =
					info.attribute_value("channels", 0U);
				if (avail_chans == 0U) {
					result = EINVAL;
					return;
				}

				int const num_chans = *(int const *)argp;
				if (num_chans < 0) {
					result = EINVAL;
					return;
				}

				if ((unsigned)num_chans != avail_chans) {
					result = ENOTSUP;
					return;
				}

				*(int *)argp = avail_chans;

				handled = true;
			});

			return Fn::COMPLETE;
		});

	} else if (request == SNDCTL_DSP_CURRENT_OPTR) {

		if (!argp) return { true, EINVAL };

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {

				if (info.type() != "oss") {
					return;
				}

				long long const optr_samples =
					info.attribute_value("optr_samples", -1L);
				int const optr_fifo_samples =
					info.attribute_value("optr_fifo_samples", -1L);
				if ((optr_samples == -1) || (optr_fifo_samples == -1)) {
					result = ENOTSUP;
					return;
				}

				oss_count_t *optr = (oss_count_t *)argp;
				optr->samples      = optr_samples;
				optr->fifo_samples = optr_fifo_samples;

				handled = true;
			});

			return Fn::COMPLETE;
		});

	} else if (request == SNDCTL_DSP_GETERROR) {

		if (!argp) return { true, EINVAL };

		int play_underruns = 0;

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() != "oss") {
					return;
				}

				play_underruns = info.attribute_value("play_underruns", 0U);
			});

			return Fn::COMPLETE;
		});


		if (play_underruns > 0) {

			/* reset */

			char const play_underruns_string[] = "0";
			Absolute_path play_underruns_path = fd.ioctl_dir();
			play_underruns_path.append_element("play_underruns");
			int play_underruns_fd = open(play_underruns_path.base(), O_RDWR);
			if (!play_underruns_fd)
				return { true, ENOTSUP };
			write(play_underruns_fd, play_underruns_string, sizeof(play_underruns_string));
			close(play_underruns_fd);
		}

		struct audio_errinfo *err_info =
			(struct audio_errinfo *)argp;

		err_info->play_underruns  = play_underruns;
		err_info->rec_overruns    = 0;
		err_info->play_ptradjust  = 0;
		err_info->rec_ptradjust   = 0;
		err_info->play_errorcount = 0;
		err_info->rec_errorcount  = 0;
		err_info->play_lasterror  = 0;
		err_info->rec_lasterror   = 0;
		err_info->play_errorparm  = 0;
		err_info->rec_errorparm   = 0;

		handled = true;

	} else if (request == SNDCTL_DSP_GETFMTS) {

		if (!argp) return { true, EINVAL };

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() != "oss") {
					return;
				}

				unsigned int const format =
					info.attribute_value("format", 0U);
				if (format == 0U) {
					result = EINVAL;
					return;
				}

				*(int *)argp = format;

				handled = true;
			});

			return Fn::COMPLETE;
		});

	} else if (request == SNDCTL_DSP_GETISPACE) {

		if (!argp) return { true, EINVAL };

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() != "oss") {
					return;
				}

				unsigned int const ifrag_size =
					info.attribute_value("ifrag_size", 0U);
				unsigned int const ifrag_avail =
					info.attribute_value("ifrag_avail", 0U);
				unsigned int const ifrag_total =
					info.attribute_value("ifrag_total", 0U);
				unsigned int const ifrag_bytes =
					info.attribute_value("ifrag_bytes", 0U);
				if (!ifrag_size || !ifrag_total) {
					result = ENOTSUP;
					return;
				}

				int const fragments  = (int)ifrag_avail;
				int const fragstotal = (int)ifrag_total;
				int const fragsize   = (int)ifrag_size;
				int const bytes      = (int)ifrag_bytes;
				if (fragments < 0 || fragstotal < 0 ||
				    fragsize < 0 || bytes < 0) {
					result = EINVAL;
					return;
				}

				struct audio_buf_info *buf_info =
					(struct audio_buf_info *)argp;

				buf_info->fragments  = fragments;
				buf_info->fragstotal = fragstotal;
				buf_info->fragsize   = fragsize;
				buf_info->bytes      = bytes;

				handled = true;
			});

			return Fn::COMPLETE;
		});

	} else if (request == SNDCTL_DSP_GETOPTR) {

		if (!argp) return { true, EINVAL };

		/* dummy implementation */

		count_info &ci = *(count_info *)argp;
		ci = {
			.bytes  = 0, /* Total # of bytes processed */
			.blocks = 0, /* # of fragment transitions since last time */
			.ptr    = 0, /* Current DMA pointer value */
		};

		handled = true;

	} else if (request == SNDCTL_DSP_GETOSPACE) {

		if (!argp) return { true, EINVAL };

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() != "oss") {
					return;
				}

				unsigned int const ofrag_size =
					info.attribute_value("ofrag_size", 0U);
				unsigned int const ofrag_avail =
					info.attribute_value("ofrag_avail", 0U);
				unsigned int const ofrag_total =
					info.attribute_value("ofrag_total", 0U);
				unsigned int const ofrag_bytes =
					info.attribute_value("ofrag_bytes", 0U);
				if (!ofrag_size || !ofrag_total) {
					result = ENOTSUP;
					return;
				}

				int const fragments  = (int)ofrag_avail;
				int const fragstotal = (int)ofrag_total;
				int const fragsize   = (int)ofrag_size;
				int const bytes      = (int)ofrag_bytes;
				if (fragments < 0 || fragstotal < 0 ||
				    fragsize < 0 || bytes < 0) {
					result = EINVAL;
					return;
				}

				struct audio_buf_info *buf_info =
					(struct audio_buf_info *)argp;

				buf_info->fragments  = fragments;
				buf_info->fragstotal = fragstotal;
				buf_info->fragsize   = fragsize;
				buf_info->bytes      = bytes;

				handled = true;
			});

			return Fn::COMPLETE;
		});

	} else if (request == SNDCTL_DSP_GETPLAYVOL) {

		if (!argp) return { true, EINVAL };

		/* dummy implementation */

		int *vol = (int *)argp;

		*vol = 100;

		handled = true;

	} else if (request == SNDCTL_DSP_LOW_WATER) {

		if (!argp) return { true, EINVAL };

		/* dummy implementation */

		int *val = (int *)argp;

		*val = 0;

		handled = true;

	} else if (request == SNDCTL_DSP_NONBLOCK) {

		/* dummy implementation */

		handled = true;

	} else if (request == SNDCTL_DSP_POST) {

		handled = true;

	} else if (request == SNDCTL_DSP_HALT) {

		if (((fd.flags & O_ACCMODE) == O_RDONLY) ||
		    ((fd.flags & O_ACCMODE) == O_RDWR)) {

			char const halt_input_string[] = "1";

			Absolute_path halt_input_path = fd.ioctl_dir();
			halt_input_path.append_element("halt_input");
			int halt_input_fd = open(halt_input_path.base(), O_WRONLY);
			if (halt_input_fd < 0)
				return { true, ENOTSUP };
			write(halt_input_fd, halt_input_string, sizeof(halt_input_string));
			close(halt_input_fd);
		}

		if (((fd.flags & O_ACCMODE) == O_WRONLY) ||
		    ((fd.flags & O_ACCMODE) == O_RDWR)) {

			char const halt_output_string[] = "1";

			Absolute_path halt_output_path = fd.ioctl_dir();
			halt_output_path.append_element("halt_output");
			int halt_output_fd = open(halt_output_path.base(), O_WRONLY);
			if (halt_output_fd < 0)
				return { true, ENOTSUP };
			write(halt_output_fd, halt_output_string, sizeof(halt_output_string));
			close(halt_output_fd);
		}

		handled = true;

	} else if (request == SNDCTL_DSP_SAMPLESIZE) {

		if (!argp) return { true, EINVAL };

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() != "oss") {
					return;
				}

				unsigned int const format =
					info.attribute_value("format", ~0U);
				if (format == ~0U) {
					result = EINVAL;
					return;
				}

				int const requested_fmt = *(int const *)argp;

				if (requested_fmt != (int)format) {
					result = ENOTSUP;
					return;
				}

				handled = true;
			});

			return Fn::COMPLETE;
		});

	} else if (request == SNDCTL_DSP_SETFRAGMENT) {

		if (!argp) return { true, EINVAL };

		int *frag = (int *)argp;
		int max_fragments = *frag >> 16;
		int size_selector = *frag & ((1<<16) - 1);

		if (((fd.flags & O_ACCMODE) == O_RDONLY) ||
		    ((fd.flags & O_ACCMODE) == O_RDWR)) {

			char ifrag_total_string[16];
			char ifrag_size_string[16];

			::snprintf(ifrag_total_string, sizeof(ifrag_total_string),
		           	   "%u", max_fragments);

			::snprintf(ifrag_size_string, sizeof(ifrag_size_string),
		           	   "%u", 1 << size_selector);

			Absolute_path ifrag_total_path = fd.ioctl_dir();
			ifrag_total_path.append_element("ifrag_total");
			int ifrag_total_fd = open(ifrag_total_path.base(), O_RDWR);
			if (ifrag_total_fd < 0)
				return { true, ENOTSUP };
			write(ifrag_total_fd, ifrag_total_string, sizeof(ifrag_total_string));
			close(ifrag_total_fd);

			Absolute_path ifrag_size_path = fd.ioctl_dir();
			ifrag_size_path.append_element("ifrag_size");
			int ifrag_size_fd = open(ifrag_size_path.base(), O_RDWR);
			if (ifrag_size_fd < 0)
				return { true, ENOTSUP };
			write(ifrag_size_fd, ifrag_size_string, sizeof(ifrag_size_string));
			close(ifrag_size_fd);

			fs._monitor.monitor([&] {

				with_info(fs, fd, [&] (Node const &info) {
					if (info.type() != "oss") {
						return;
					}

					unsigned int const ifrag_size = info.attribute_value("ifrag_size", 0u);
					uint8_t const ifrag_size_log2 = Genode::log2(ifrag_size, 0u);

					unsigned int const ifrag_total =
						info.attribute_value("ifrag_total", 0U);

					if (!ifrag_total || !ifrag_size_log2) {
						result = ENOTSUP;
						return;
					}
				});
				
				return Fn::COMPLETE;
			});
		}

		if (((fd.flags & O_ACCMODE) == O_WRONLY) ||
		    ((fd.flags & O_ACCMODE) == O_RDWR)) {

			char ofrag_total_string[16];
			char ofrag_size_string[16];

			::snprintf(ofrag_total_string, sizeof(ofrag_total_string),
		           	   "%u", max_fragments);

			::snprintf(ofrag_size_string, sizeof(ofrag_size_string),
		           	   "%u", 1 << size_selector);

			Absolute_path ofrag_total_path = fd.ioctl_dir();
			ofrag_total_path.append_element("ofrag_total");
			int ofrag_total_fd = open(ofrag_total_path.base(), O_RDWR);
			if (ofrag_total_fd < 0)
				return { true, ENOTSUP };
			write(ofrag_total_fd, ofrag_total_string, sizeof(ofrag_total_string));
			close(ofrag_total_fd);

			Absolute_path ofrag_size_path = fd.ioctl_dir();
			ofrag_size_path.append_element("ofrag_size");
			int ofrag_size_fd = open(ofrag_size_path.base(), O_RDWR);
			if (ofrag_size_fd < 0)
				return { true, ENOTSUP };
			write(ofrag_size_fd, ofrag_size_string, sizeof(ofrag_size_string));
			close(ofrag_size_fd);

			fs._monitor.monitor([&] {

				with_info(fs, fd, [&] (Node const &info) {
					if (info.type() != "oss") {
						return;
					}

					unsigned int const ofrag_size = info.attribute_value("ofrag_size", 0U);
					uint8_t const ofrag_size_log2 = Genode::log2(ofrag_size, 0u);

					unsigned int const ofrag_total =
						info.attribute_value("ofrag_total", 0U);

					if (!ofrag_total || !ofrag_size_log2) {
						result = ENOTSUP;
						return;
					}
				});

				return Fn::COMPLETE;
			});
		}

		handled = true;

	} else if (request == SNDCTL_DSP_SETPLAYVOL) {

		if (!argp) return { true, EINVAL };

		/* dummy implementation */

		int *vol = (int *)argp;

		*vol = 100;

		handled = true;

	} else if (request == SNDCTL_DSP_SETTRIGGER) {

		if (!argp) return { true, EINVAL };

		int mask = *(int *)argp;

		if (((fd.flags & O_ACCMODE) == O_RDONLY) ||
		    ((fd.flags & O_ACCMODE) == O_RDWR)) {

			char enable_input_string[2];

			::snprintf(enable_input_string, sizeof(enable_input_string),
			           "%u", (mask & PCM_ENABLE_INPUT) ? 1 : 0);

			Absolute_path enable_input_path = fd.ioctl_dir();
			enable_input_path.append_element("enable_input");
			int enable_input_fd = open(enable_input_path.base(), O_WRONLY);
			if (enable_input_fd < 0)
				return { true, ENOTSUP };
			write(enable_input_fd, enable_input_string, sizeof(enable_input_string));
			close(enable_input_fd);
		}

		if (((fd.flags & O_ACCMODE) == O_WRONLY) ||
		    ((fd.flags & O_ACCMODE) == O_RDWR)) {

			char enable_output_string[2];

			::snprintf(enable_output_string, sizeof(enable_output_string),
			           "%u", (mask & PCM_ENABLE_OUTPUT) ? 1 : 0);

			Absolute_path enable_output_path = fd.ioctl_dir();
			enable_output_path.append_element("enable_output");
			int enable_output_fd = open(enable_output_path.base(), O_WRONLY);
			if (enable_output_fd < 0)
				return { true, ENOTSUP };
			write(enable_output_fd, enable_output_string, sizeof(enable_output_string));
			close(enable_output_fd);
		}

		handled = true;

	} else if (request == SNDCTL_DSP_SPEED) {

		if (!argp) return { true, EINVAL };

		int const sample_rate = *(int const *)argp;
		if (sample_rate < 0)
			return { true, EINVAL };

		bool legacy_oss = false;

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() != "oss") return;

				/* assume legacy if version is not set, current is 2 */
				legacy_oss = info.attribute_value("plugin_version", 1U) == 1U;
			});

			return Fn::COMPLETE;
		});

		if (!legacy_oss) {

			char sample_rate_string[8] { };

			::snprintf(sample_rate_string, sizeof(sample_rate_string), "%u", sample_rate);

			Absolute_path sample_rate_path = fd.ioctl_dir();
			sample_rate_path.append_element("sample_rate");
			int sample_rate_fd = open(sample_rate_path.base(), O_RDWR);
			if (sample_rate_fd < 0)
				return { true, ENOTSUP };
			write(sample_rate_fd, sample_rate_string, sizeof(sample_rate_string));
			close(sample_rate_fd);
		}

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() != "oss") {
					return;
				}

				unsigned int const got_sample_rate =
					info.attribute_value("sample_rate", 0U);
				if (got_sample_rate == 0U) {
					result = EINVAL;
					return;
				}

				if ((unsigned)sample_rate != got_sample_rate) {
					result = ENOTSUP;
					return;
				}

				*(int *)argp = got_sample_rate;

				handled = true;
			});

			return Fn::COMPLETE;
		});

	} else if (request == SNDCTL_DSP_SYNC) {

		/*
		 * SNDCTL_DSP_SYNC should be implemented like follows, but we disabled
		 * the implementation and just return for two reasons. The VirtualBox 6
		 * backend requires the sync operation to complete with very tight
		 * timing in a special thread, which our current implementation can't
		 * assure. Also, the OSS documentation (and examples) advise against the
		 * use of this feature in new programs.
		 */
		if (0) fs._monitor.monitor([&] {

			auto result = Fn::INCOMPLETE;

			with_info(fs, fd, [&] (Node const &info) {

				if (info.type() != "oss") return;

				unsigned int const ofrag_avail =
					info.attribute_value("ofrag_avail", 0U);
				unsigned int const ofrag_total =
					info.attribute_value("ofrag_total", 0U);

				result = (ofrag_avail == ofrag_total ? Fn::COMPLETE : Fn::INCOMPLETE);
			});

			return result;
		});

		handled = true;

	} else if (request == SNDCTL_SYSINFO) {

		if (!argp) return { true, EINVAL };

		/* dummy implementation */

		oss_sysinfo *si = (oss_sysinfo *)argp;
		Genode::bzero(si, sizeof(*si));

		handled = true;
	}

	/*
	 * Either handled or a failed attempt will mark the I/O control
	 * as handled.
	 */
	return { handled || result != 0, result };
}


static Ioctl_result ioctl_tapctl(Libc::Fs &fs, Libc::File_descriptor &fd,
                                 unsigned long request, char *argp)
{
	using namespace Libc;

	using Absolute_path = Genode::Path<PATH_MAX>;

	bool handled = false;
	int  result  = 0;

	if (request == TAPGIFNAME) {       /* return device name */
		if (!argp)
			return { true, EINVAL };

		ifreq *ifr = reinterpret_cast<ifreq*>(argp);

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() == "tap") {
					String<IFNAMSIZ> name = info.attribute_value("name", String<IFNAMSIZ> { });
					copy_cstring(ifr->ifr_name, name.string(), IFNAMSIZ);
					handled = true;
				}
			});

			return Fn::COMPLETE;
		});
	}
	else if (request == SIOCGIFADDR) { /* get MAC address */
		if (!argp)
			return { true, EINVAL };

		fs._monitor.monitor([&] {
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() == "tap") {
					Net::Mac_address mac = info.attribute_value("mac_addr", Net::Mac_address { });
					mac.copy(argp);
					handled = true;
				}
			});

			return Fn::COMPLETE;
		});
	}
	else if (request == SIOCSIFADDR) { /* set MAC address */
		if (!argp)
			return { true, EINVAL };

		Net::Mac_address new_mac    { argp };
		String<18>       mac_string { new_mac };

		/* write string into file */
		Absolute_path mac_addr_path = fd.ioctl_dir();
		mac_addr_path.append_element("mac_addr");
		int mac_addr_fd = open(mac_addr_path.base(), O_RDWR);
		if (mac_addr_fd < 0)
			return { true, ENOTSUP };
		write(mac_addr_fd, mac_string.string(), mac_string.length());
		close(mac_addr_fd);

		fs._monitor.monitor([&] {
			/* check whether mac address changed, return ENOTSUP if not */
			with_info(fs, fd, [&] (Node const &info) {
				if (info.type() == "tap") {
					if (!info.has_attribute("mac_addr"))
						result = ENOTSUP;
					else {
						Net::Mac_address cur_mac = info.attribute_value("mac_addr", Net::Mac_address { });
						if (cur_mac != new_mac)
							result = ENOTSUP;
					}

					handled = true;
				}
			});

			return Fn::COMPLETE;
		});
	}

	return { handled, result };
}


int Libc::Fs::ioctl(File_descriptor &fd, unsigned long request, char *argp)
{
	Ioctl_result result { false, 0 };

	/* we need libc's off_t which is int64_t */
	using ::off_t;

	switch (request) {
	case TIOCGWINSZ:
	case TIOCFLUSH:
	case TIOCGETA:
	case TIOCSETA:
		result = ioctl_tio(*this, fd, request, argp);
		break;
	case DIOCGMEDIASIZE:
		result = ioctl_dio(*this, fd, request, argp);
		break;
	case OSS_GETVERSION:
	case SNDCTL_DSP_CHANNELS:
	case SNDCTL_DSP_CURRENT_OPTR:
	case SNDCTL_DSP_GETERROR:
	case SNDCTL_DSP_GETFMTS:
	case SNDCTL_DSP_GETISPACE:
	case SNDCTL_DSP_GETOPTR:
	case SNDCTL_DSP_GETOSPACE:
	case SNDCTL_DSP_GETPLAYVOL:
	case SNDCTL_DSP_LOW_WATER:
	case SNDCTL_DSP_NONBLOCK:
	case SNDCTL_DSP_POST:
	case SNDCTL_DSP_RESET:
	case SNDCTL_DSP_SAMPLESIZE:
	case SNDCTL_DSP_SETFRAGMENT:
	case SNDCTL_DSP_SETPLAYVOL:
	case SNDCTL_DSP_SETTRIGGER:
	case SNDCTL_DSP_SPEED:
	case SNDCTL_DSP_SYNC:
	case SNDCTL_SYSINFO:
		result = ioctl_sndctl(*this, fd, request, argp);
		break;
	case TAPSIFINFO:
	case TAPGIFINFO:
	case TAPSDEBUG:
	case TAPGDEBUG:
	case TAPGIFNAME:
	case SIOCGIFADDR:
	case SIOCSIFADDR:
		result = ioctl_tapctl(*this, fd, request, argp);
		break;
	default:
		break;
	}

	if (!result.handled)
		return Errno(EINVAL);

	return result.error ? Errno(result.error) : 0;
}


void Libc::Fs::lseek_from_kernel(File_descriptor &fd, ::off_t offset)
{
	if (!fd.open_file_ptr) {
		error("lseek_from_kernel called for non-file descriptor");
		return;
	}

	fd.open_file_ptr->handle.seek(offset);
}


::off_t Libc::Fs::lseek(File_descriptor &fd, ::off_t offset, int whence)
{
	if (!fd.open_file_ptr && !fd.open_dir_ptr)
		return Errno(EBADF);

	Vfs::Vfs_handle &handle = fd.open_file_ptr ? fd.open_file_ptr->handle
	                                           : fd.open_dir_ptr->handle;

	switch (whence) {
	case SEEK_SET: handle.seek(offset); break;
	case SEEK_CUR: handle.advance_seek(offset); break;
	case SEEK_END:
		{
			struct stat stat;
			::memset(&stat, 0, sizeof(stat));
			fstat(fd, stat);
			handle.seek(stat.st_size + offset);
		}
		break;
	}
	return handle.seek();
}


int Libc::Fs::ftruncate(Open_file &of, off_t length)
{
	Sync sync { of.handle, { .update_mtime = _config.update_mtime }, _now };

	bool succeeded = false;
	int result_errno = 0;
	_monitor.monitor([&] {
		if (of.modified) {
			if (!sync.complete()) {
				return Fn::INCOMPLETE;
			}
			of.modified = false;
		}

		using Result = Vfs::Ftruncate_result;

		switch (of.handle.ftruncate(length)) {
		case Result::FTRUNCATE_ERR_NO_PERM:   result_errno = EPERM;  break;
		case Result::FTRUNCATE_ERR_INTERRUPT: result_errno = EINTR;  break;
		case Result::FTRUNCATE_ERR_NO_SPACE:  result_errno = ENOSPC; break;
		case Result::FTRUNCATE_OK:               succeeded = true;   break;
		}
		return Fn::COMPLETE;
	});
	return succeeded ? 0 : Errno(result_errno);
}


void Libc::Fs::fsync(Open_file &of)
{
	if (!of.modified)
		return;

	Sync sync { of.handle, { .update_mtime = _config.update_mtime }, _now };

	_monitor.monitor([&] {
		if (!sync.complete()) {
			return Fn::INCOMPLETE;
		}
		return Fn::COMPLETE;
	});

	of.modified = false;
}


int Libc::Fs::symlink(char const *target_path, const char *link_path)
{
	Vfs::Vfs_handle *handle_ptr = nullptr;
	Constructible<Sync> sync;

	size_t const count = ::strlen(target_path) + 1;

	size_t out_count = 0;

	{
		bool succeeded { false };
		int result_errno { 0 };
		_monitor.monitor([&] {

			using Openlink_result = Vfs::Directory_service::Openlink_result;

			Openlink_result openlink_result =
				_vfs.openlink(link_path, true, &handle_ptr, _kernel_heap);

			switch (openlink_result) {
			case Openlink_result::OPENLINK_ERR_LOOKUP_FAILED:
				result_errno = ENOENT; return Fn::COMPLETE;
			case Openlink_result::OPENLINK_ERR_NAME_TOO_LONG:
				result_errno = ENAMETOOLONG; return Fn::COMPLETE;
			case Openlink_result::OPENLINK_ERR_NODE_ALREADY_EXISTS:
				result_errno = EEXIST; return Fn::COMPLETE;
			case Openlink_result::OPENLINK_ERR_NO_SPACE:
				result_errno = ENOSPC; return Fn::COMPLETE;
			case Openlink_result::OPENLINK_ERR_OUT_OF_RAM:
				result_errno = ENOSPC; return Fn::COMPLETE;
			case Openlink_result::OPENLINK_ERR_OUT_OF_CAPS:
				result_errno = ENOSPC; return Fn::COMPLETE;
			case Vfs::Directory_service::OPENLINK_ERR_PERMISSION_DENIED:
				result_errno = EPERM; return Fn::COMPLETE;
			case Openlink_result::OPENLINK_OK:
				break;
			}

			succeeded = true;
			return Fn::COMPLETE;
		});

		if (!succeeded)
			return Errno(result_errno);
	}

	Vfs::Vfs_handle &handle = *handle_ptr;
	handle.handler(&_response_handler);

	/* must be done outside the monitor because constructor needs libc I/O */
	sync.construct(handle, Sync::Attr { .update_mtime = _config.update_mtime }, _now);
	{
		bool succeeded { false };
		int result_errno { 0 };
		enum class Stage { WRITE, SYNC } stage = Stage::WRITE;

		_monitor.monitor([&] {

			switch (stage) {

			case Stage::WRITE:
				{
					using Result = Vfs::Write_result;

					Const_byte_range_ptr const src { target_path, count };

					Result result = handle.write(src, out_count);

					if (result == Result::WRITE_ERR_WOULD_BLOCK)
						return Fn::INCOMPLETE;
				}
				stage = Stage::SYNC;
				[[fallthrough]];

			case Stage::SYNC:
				{
					if (!sync->complete())
						return Fn::INCOMPLETE;
					handle.close();
				} break;
			}

			if (out_count != count)
				result_errno = ENAMETOOLONG;
			else
				succeeded = true;
			return Fn::COMPLETE;
		});

		if (!succeeded)
		return Errno(result_errno);
	}

	return 0;
}


ssize_t Libc::Fs::readlink(const char *link_path, char *buf, ::size_t buf_size)
{
	enum class Stage { OPEN, READ };

	Stage stage { Stage::OPEN };

	Vfs::Vfs_handle *handle_ptr = nullptr;

	::size_t out_count    = 0;
	bool     succeeded    = false;
	int      result_errno = 0;

	_monitor.monitor([&] {

		switch (stage) {
		case Stage::OPEN:
			{
				using Openlink_result = Vfs::Directory_service::Openlink_result;

				Openlink_result openlink_result =
					_vfs.openlink(link_path, false, &handle_ptr, _kernel_heap);

				switch (openlink_result) {
				case Openlink_result::OPENLINK_ERR_LOOKUP_FAILED:
					result_errno = ENOENT; return Fn::COMPLETE;
				case Openlink_result::OPENLINK_ERR_NAME_TOO_LONG:
					/* should not happen */
					result_errno = ENAMETOOLONG; return Fn::COMPLETE;
				case Openlink_result::OPENLINK_ERR_NODE_ALREADY_EXISTS:
				case Openlink_result::OPENLINK_ERR_NO_SPACE:
				case Openlink_result::OPENLINK_ERR_OUT_OF_RAM:
				case Openlink_result::OPENLINK_ERR_OUT_OF_CAPS:
				case Openlink_result::OPENLINK_ERR_PERMISSION_DENIED:
					result_errno = EACCES; return Fn::COMPLETE;
				case Openlink_result::OPENLINK_OK:
					break;
				}

				handle_ptr->handler(&_response_handler);
			}
			stage = Stage::READ; [[ fallthrough ]];

		case Stage::READ:
			{
				Byte_range_ptr const dst { buf, buf_size };

				Vfs::Read_result const result = handle_ptr->read(dst);
				if (result == Vfs::Read_error::RETRY)
					return Fn::INCOMPLETE;

				handle_ptr->close();

				result.with_result(
					[&] (size_t num_bytes) {
						out_count = num_bytes;
						succeeded = true;
					},
					[&] (Vfs::Read_error) {
						result_errno = EINVAL; });
			}
			break;
		}

		return Fn::COMPLETE;
	});

	if (!succeeded)
		return Errno(result_errno);

	return out_count;
}


int Libc::Fs::unlink(char const *path)
{
	using Result = Vfs::Directory_service::Unlink_result;

	bool succeeded = false;
	int result_errno = 0;
	_monitor.monitor([&] {
		switch (_vfs.unlink(path)) {
		case Result::UNLINK_ERR_NO_ENTRY:  result_errno = ENOENT;    break;
		case Result::UNLINK_ERR_NO_PERM:   result_errno = EPERM;     break;
		case Result::UNLINK_ERR_NOT_EMPTY: result_errno = ENOTEMPTY; break;
		case Result::UNLINK_OK:               succeeded = true;      break;
		}
		return Fn::COMPLETE;
	});
	if (!succeeded)
		return Errno(result_errno);

	return 0;
}


int Libc::Fs::rename(char const *from_path, char const *to_path)
{
	using Result = Vfs::Directory_service::Rename_result;

	bool succeeded = false;
	int result_errno = false;
	_monitor.monitor([&] {
		if (_vfs.dir_entry_exists(to_path)) {
			if (_vfs.directory(to_path)) {
				if (!_vfs.directory(from_path)) {
					result_errno = EISDIR; return Fn::COMPLETE;
				}

				if (_vfs.num_dirent(to_path)) {
					result_errno = ENOTEMPTY; return Fn::COMPLETE;
				}

			} else {
				if (_vfs.directory(from_path)) {
					result_errno = ENOTDIR; return Fn::COMPLETE;
				}
			}
		}

		switch (_vfs.rename(from_path, to_path)) {
		case Result::RENAME_ERR_NO_ENTRY: result_errno = ENOENT; break;
		case Result::RENAME_ERR_CROSS_FS: result_errno = EXDEV;  break;
		case Result::RENAME_ERR_NO_PERM:  result_errno = EPERM;  break;
		case Result::RENAME_OK:       succeeded = true;   break;
		}
		return Fn::COMPLETE;
	});

	if (!succeeded)
		return Errno(result_errno);

	return 0;
}


namespace Libc { struct Mmap_entry; };

struct Libc::Mmap_entry : Registry<Mmap_entry>::Element
{
	void * const start;

	Vfs::Vfs_handle &reference_handle;

	Mmap_entry(Registry<Mmap_entry> &registry,
	           void *start, Vfs::Vfs_handle &reference_handle)
	:
		Registry<Mmap_entry>::Element(registry, *this), start(start),
		reference_handle(reference_handle)
	{ }
};


static Genode::Registry<Libc::Mmap_entry> &mmap_registry()
{
	static Genode::Registry<Libc::Mmap_entry> inst { };
	return inst;
}


void *Libc::Fs::mmap(File_descriptor &fd, void *addr_in, ::size_t length,
                     int prot, int flags, ::off_t offset)
{
	if ((prot != PROT_READ) && (prot != (PROT_READ | PROT_WRITE))) {
		error("mmap for prot=", Hex(prot), " not supported");
		errno = EACCES;
		return MAP_FAILED;
	}

	if (flags & MAP_FIXED) {
		error("mmap for fixed predefined address not supported yet");
		errno = EINVAL;
		return MAP_FAILED;
	}

	void *addr = nullptr;

	if (flags & MAP_PRIVATE) {

		/*
		 * XXX attempt to obtain memory mapping via
		 *     'Vfs::Directory_service::dataspace'.
		 */

		addr = mem_alloc()->alloc(length, AT_PAGE);
		if (addr == (void *)-1) {
			error("mmap out of memory");
			errno = ENOMEM;
			return MAP_FAILED;
		}

		/* copy variables for complete read */
		size_t read_remain = length;
		size_t read_offset = offset;
		char *read_addr = (char *)addr;

		while (read_remain > 0) {
			ssize_t length_read = ::pread(fd.libc_fd, read_addr, read_remain, read_offset);
			if (length_read < 0) { /* error */
				error("mmap could not obtain file content");
				::munmap(addr, length);
				errno = EACCES;
				return MAP_FAILED;
			} else if (length_read == 0) /* EOF */
				break; /* done (length can legally be greater than the file length) */
			read_remain -= length_read;
			read_offset += length_read;
			read_addr += length_read;
		}

	} else if (flags & MAP_SHARED) {

		/* create another VFS handle to keep the file open as long as the mapping exists */

		if (!fd.open_file_ptr) {
			error("attempt to mmap non-file fd via MAP_SHARED");
			errno = EBADF;
			return MAP_FAILED;
		}

		Vfs::Vfs_handle *reference_handle_ptr = nullptr;
		using Result = Vfs::Directory_service::Open_result;
		Result vfs_open_result;
		_monitor.monitor([&] {
			vfs_open_result = _vfs.open(fd.path.string(), fd.flags,
			                            &reference_handle_ptr, _kernel_heap);
			return Fn::COMPLETE;
		});

		if (vfs_open_result != Result::OPEN_OK) {
			error("mmap could not create reference VFS handle");
			errno = ENFILE;
			return MAP_FAILED;
		}

		Genode::Dataspace_capability ds_cap;

		_monitor.monitor([&] {
			ds_cap = _vfs.dataspace(fd.path.string());
			return Fn::COMPLETE;
		});

		if (!ds_cap.valid()) {
			Genode::error("mmap got invalid dataspace capability");
			_monitor.monitor([&] {
				reference_handle_ptr->close();
				return Fn::COMPLETE;
			});
			errno = ENODEV;
			return MAP_FAILED;
		}

		addr = _local_rm.attach(ds_cap, {
			.size       = length,
			.offset     = addr_t(offset),
			.use_at     = { },
			.at         = { },
			.executable = { },
			.writeable  = true
		}).convert<void *>(
			[&] (Env::Local_rm::Attachment &a) { a.deallocate = false; return a.ptr; },
			[&] (Env::Local_rm::Error)         { return nullptr; }
		);

		if (!addr) {
			_monitor.monitor([&] {
				reference_handle_ptr->close();
				return Fn::COMPLETE;
			});
			errno = ENOMEM;
			return MAP_FAILED;
		}

		new (_kernel_heap) Mmap_entry(mmap_registry(), addr, *reference_handle_ptr);
	}

	return addr;
}


int Libc::Fs::munmap(void *addr, ::size_t)
{
	using Size_at_error = Mem_alloc::Size_at_error;

	Mem_alloc::Size_at_result const size_at_result = mem_alloc()->size_at(addr);

	if (size_at_result.ok()) {
		/* private mapping */
		size_at_result.with_result(
			[&] (size_t)        { mem_alloc()->free(addr); },
			[&] (Size_at_error) {                          });

		return 0;
	}

	/* return error if addr is not a block start address */
	if (size_at_result == Size_at_error::MISMATCHING_ADDR)
		return Errno(EINVAL);

	/* shared mapping */

	Vfs::Vfs_handle *reference_handle_ptr = nullptr;

	mmap_registry().for_each([&] (Mmap_entry &entry) {
		if (entry.start == addr) {
			reference_handle_ptr = &entry.reference_handle;
			Genode::destroy(_kernel_heap, &entry);
			_local_rm.detach(addr_t(addr));
		}
	});

	if (!reference_handle_ptr)
		return Errno(EINVAL);

	_monitor.monitor([&] {
		reference_handle_ptr->close();
		return Fn::COMPLETE;
	});

	return 0;
}


int Libc::Fs::poll(Monitor &monitor, Pollfd fds[], int nfds)
{
	int nready = 0;

	auto fn = [&] {

		for (int pollfd_index = 0; pollfd_index < nfds; pollfd_index++) {

			File_descriptor * const fd_ptr = fds[pollfd_index].fdo;
			if (!fd_ptr)
				continue;

			if (!fd_ptr->open_file_ptr)
				continue;

			Open_file &of = *fd_ptr->open_file_ptr;

			bool fd_ready = false;

			if (fds[pollfd_index].events & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)) {
				if (of.handle.read_ready()) {
					*fds[pollfd_index].revents |= POLLIN;
					fd_ready = true;
				} else {
					of.handle.notify_read_ready();
				}
			}

			if (fds[pollfd_index].events & (POLLOUT | POLLWRNORM | POLLWRBAND)) {
				if (of.handle.write_ready()) {
					*fds[pollfd_index].revents |= POLLOUT;
					fd_ready = true;
				}
			}

			/* XXX POLLERR not supported */

			if (fd_ready)
				nready++;
		}
		return Fn::COMPLETE;
	};

	if (Libc::Kernel::kernel().main_context() && Libc::Kernel::kernel().main_suspended()) {
		fn();
	} else {
		monitor.monitor(fn);
	}

	return nready;
}


static bool _handle_aio_read(Libc::File_descriptor          &fd,
                             Libc::File_descriptor::Aio_job &aio_job)
{
	using Aio_job    = Libc::File_descriptor::Aio_job;
	using Aio_handle = Libc::File_descriptor::Aio_handle;
	using Vfs_handle = Genode::Vfs::Vfs_handle;
	using Result     = Genode::Vfs::Read_result;

	bool progress = false;

	aio_job.with_aio_handle([&] (Aio_handle &aio_handle) {
		aio_handle.with_vfs_handle([&] (Vfs_handle &vfs_handle) {

			switch (aio_handle.state) {
			case Aio_handle::State::INVALID:
			{
				if ((fd.flags & O_ACCMODE) == O_WRONLY) {
					aio_job.result = -1;
					aio_job.error  = EBADF;
					aio_job.state  = Aio_job::State::COMPLETE;
					break;
				}

				aio_handle.state = Aio_handle::State::QUEUED;
				progress = true;

				[[fallthrough]];
			}
			case Aio_handle::State::QUEUED:
			{
				vfs_handle.seek(aio_job.iocb->aio_offset);
				Genode::Byte_range_ptr const dst {
					(char *)aio_job.iocb->aio_buf, aio_job.iocb->aio_nbytes };

				Result const result = vfs_handle.read(dst);
				if (result == Genode::Vfs::Read_error::RETRY)
					break;

				result.with_result(
					[&] (size_t num_bytes) {
						aio_job.result = num_bytes;
						aio_job.error  = 0;
					},
					[&] (Genode::Vfs::Read_error) {
						aio_job.result = -1;
						aio_job.error  = EINVAL;
					});

				aio_handle.state = Aio_handle::State::COMPLETE;
				progress = true;
			}
			case Aio_handle::State::COMPLETE:
				aio_job.state = Aio_job::State::COMPLETE;
				++fd.lio_list_completed;
				break;
			}
		});
	});

	if (aio_job.state == Aio_job::State::COMPLETE)
		aio_job.release_handle();

	return progress;
}


static bool _handle_aio_write(Libc::File_descriptor          &fd,
                              Libc::File_descriptor::Aio_job &aio_job)
{
	using Aio_job    = Libc::File_descriptor::Aio_job;
	using Aio_handle = Libc::File_descriptor::Aio_handle;
	using Vfs_handle = Genode::Vfs::Vfs_handle;
	using Result     = Genode::Vfs::Write_result;

	bool progress = false;

	aio_job.with_aio_handle([&] (Aio_handle &aio_handle) {
		aio_handle.with_vfs_handle([&] (Vfs_handle &vfs_handle) {

			switch (aio_handle.state) {
			case Aio_handle::State::INVALID:
			{
				if ((fd.flags & O_ACCMODE) == O_RDONLY) {
					aio_job.result = -1;
					aio_job.error  = EBADF;
					aio_job.state = Aio_job::State::COMPLETE;
					break;
				}

				aio_job.result = 0;
				aio_job.error  = 0;

				vfs_handle.seek(aio_job.iocb->aio_offset);

				aio_handle.count = aio_job.iocb->aio_nbytes;
				aio_handle.offset = 0;

				aio_handle.state = Aio_handle::State::QUEUED;
				progress = true;
				break;
			}
			case Aio_handle::State::QUEUED:
			{
				Genode::Const_byte_range_ptr const src {
					(char *)aio_job.iocb->aio_buf + aio_handle.offset, aio_handle.count };
				::size_t out_count = 0;
				Result const out_result = vfs_handle.write(src, out_count);

				if (out_result == Result::WRITE_OK) {
					aio_handle.count  -= out_count;
					aio_handle.offset += out_count;

					aio_job.result += out_count;

					vfs_handle.advance_seek(out_count);

					if (!aio_handle.count)
						aio_handle.state = Aio_handle::State::COMPLETE;

					progress = true;
					break;
				}

				if (out_result != Result::WRITE_ERR_WOULD_BLOCK) {
					switch (out_result) {
					case Result::WRITE_ERR_INVALID:
						aio_job.result = -1;
						aio_job.error  = EINVAL;
						break;
					case Result::WRITE_ERR_IO:
						aio_job.result = -1;
						aio_job.error  = EIO;
						break;
					case Result::WRITE_ERR_WOULD_BLOCK: /* never reached */ break;
					case Result::WRITE_OK:              /* never reached */ break;
					}
					aio_handle.state = Aio_handle::State::COMPLETE;
					progress = true;
				}
				break;
			}
			case Aio_handle::State::COMPLETE:
				if (fd.open_file_ptr) fd.open_file_ptr->modified = true;

				aio_job.state = Aio_job::State::COMPLETE;
				++fd.lio_list_completed;
				break;
			}
		});
	});

	if (aio_job.state == Aio_job::State::COMPLETE)
		aio_job.release_handle();

	return progress;
}


static bool _handle_aio_nop(Libc::File_descriptor          &fd,
                            Libc::File_descriptor::Aio_job &aio_job)
{
	using Aio_job = Libc::File_descriptor::Aio_job;

	aio_job.result = 0;
	aio_job.error  = 0 ;
	aio_job.state = Aio_job::State::COMPLETE;
	aio_job.release_handle();

	++fd.lio_list_completed;

	return false;
}

int Libc::Fs::wait_aio(Libc::File_descriptor &fd, int /*timeout_ms*/)
{
	if (fd.lio_list_completed && fd.lio_list_queued == 0)
		return 0;

	if (fd.lio_list_queued == 0)
		return Errno(EINVAL);

	using Aio_job    = Libc::File_descriptor::Aio_job;
	using Aio_handle = Libc::File_descriptor::Aio_handle;
	using Vfs_handle = Vfs::Vfs_handle;

	if (!fd.open_file_ptr) {
		error("wait_aio called for non-file fd (", fd.path, ")");
		return Errno { EBADF };
	}

	fd.for_each_aio_job(Aio_job::State::PENDING, [&] (Aio_job &aio_job) {
		fd.any_unused_aio_handle([&] (Aio_handle &aio_handle) {

			if (aio_handle.vfs_handle == nullptr) {

				auto open_vfs_handle = [&] (char const *path) -> Vfs_handle* {
					Vfs_handle *vfs_handle = nullptr;

					/*
					 * We could already open the path once as otherwise we
					 * would not be here.
					 */
					using Result = Vfs::Directory_service::Open_result;
					Result const open_result = _vfs.open(path, fd.flags,
					                                     &vfs_handle, _kernel_heap);
					return open_result == Result::OPEN_OK ? vfs_handle
					                                      : nullptr;
				};
				_monitor.monitor([&] {
					aio_handle.vfs_handle = open_vfs_handle(fd.path.string());
					return Fn::COMPLETE;
				});

				/*
				 * This should not happen and at this point we bail
				 * alltogether for now.
				 */
				if (aio_handle.vfs_handle == nullptr) {
					aio_job.result = -1;
					aio_job.error = EIO;
					aio_job.state = Aio_job::State::COMPLETE;
					return;
				}
			}

			switch (aio_job.iocb->aio_lio_opcode) {
			case LIO_READ: [[fallthrough]];
			case LIO_WRITE:
			case LIO_NOP:
				aio_job.acquire_handle(aio_handle);
				aio_job.state = Aio_job::State::IN_PROGRESS;

				--fd.lio_list_queued;
				break;
			}
		});
	});

	unsigned count_in_progress = 0;
	fd.for_each_aio_job(Aio_job::State::IN_PROGRESS, [&] (Aio_job &aio_job) {
		++count_in_progress; });

	_monitor.monitor([&] {
		fd.for_each_aio_job(Aio_job::State::IN_PROGRESS, [&] (Aio_job &aio_job) {

			/*
			 * Try one aio_job as long as some progress is made and move
			 * on to the next one in case the operation stalled (i.e.
			 * was queued).
			 */
			bool progress = false;
			do {
				switch (aio_job.iocb->aio_lio_opcode) {
				case LIO_READ:  progress = _handle_aio_read (fd, aio_job); break;
				case LIO_WRITE: progress = _handle_aio_write(fd, aio_job); break;
				case LIO_NOP:   progress = _handle_aio_nop  (fd, aio_job); break;
				}
			} while (progress);
		});
		return (fd.lio_list_completed >= count_in_progress) ? Fn::COMPLETE
		                                                    : Fn::INCOMPLETE;
	});

	return 0;
}


int Libc::Fs::enqueue_aiocb(File_descriptor &fd, struct aiocb const &iocb)
{
	using Aio_job = Libc::File_descriptor::Aio_job;

	return fd.any_free_aio_job([&] (Aio_job &aio_job) {
		aio_job.iocb  = &iocb;
		aio_job.state = Aio_job::State::PENDING;
		++fd.lio_list_queued;
	}) ? 0 : Errno(EAGAIN);
}
