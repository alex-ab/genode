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
static void vfs_stat_to_libc_stat_struct(Genode::Vfs::File_system::Stat const &src,
                                         char const *path, struct stat &dst)
{
	using namespace Genode;

	enum { FS_BLOCK_SIZE = 4096 * 16 };

	unsigned const readable_bits   = S_IRUSR,
	               writeable_bits  = S_IWUSR,
	               executable_bits = S_IXUSR;

	auto type = [] (Genode::Vfs::Dirent_type type)
	{
		switch (type) {
		case Vfs::Dirent_type::DIRECTORY: return S_IFDIR;
		case Vfs::Dirent_type::FILE:      return S_IFREG;
		case Vfs::Dirent_type::DEVICE:    return S_IFCHR;
		case Vfs::Dirent_type::SYMLINK:   return S_IFLNK;
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
	dst.st_dev     = 1;
	dst.st_mtim    = mtime;
	dst.st_nlink   = 1;
}


static Genode::Vfs::Timestamp timestamp_now(Libc::Fs &fs)
{
	Genode::Vfs::Timestamp result { };
	if (fs._config.update_mtime && fs._now.has_real_time()) {
		timespec const ts = fs._now.current_real_time();
		result.ms_since_1970 = ts.tv_sec >= 0
		                     ? ts.tv_sec*1000ull + ts.tv_nsec/1000000ull
		                     : 0;
	}
	return result;
}


namespace Libc {

	bool read_ready_from_kernel(File_descriptor &fd)
	{
		if (!fd.open_file_ptr)
			return false;

		return fd.open_file_ptr->handle.read_ready() == Vfs::Read_ready_result::YES;
	}

	bool write_ready_from_kernel(File_descriptor &fd)
	{
		if (!fd.open_file_ptr)
			return false;

		return fd.open_file_ptr->handle.write_ready() == Vfs::Write_ready_result::YES;
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


Libc::Fs::Open_file_result Libc::Fs::open_file_from_kernel(Open_file_attr const &attr)
{
	return *new (_kernel_heap) Open_file(_vfs_env, _response_handler, attr);
}


Libc::Fs::Open_file_result Libc::Fs::open_file(Open_file_attr const &attr)
{
	Open_file *result_of_ptr { };
	int        result_errno  { };

	_monitor.monitor([&] {

		if (!result_of_ptr)
			open_file_from_kernel(attr).with_result(
				[&] (Open_file &of) { result_of_ptr = &of; },
				[&] (Errno e)       { result_errno  = e; });

		if (result_of_ptr)
			return result_of_ptr->handle.attach().convert<Monitor::Function_result>(
				[&] (Ok) { return Fn::COMPLETE; },
				[&] (Vfs::File_handle::Attach_error e) {
					if (e == Vfs::File_handle::Attach_error::RETRY)
						return Fn::INCOMPLETE;

					Genode::destroy(_kernel_heap, result_of_ptr);
					result_errno = Errno(EPERM);
					result_of_ptr = nullptr;
					return Fn::COMPLETE;
				});

		assert(false);
	});

	if (result_of_ptr)
		return *result_of_ptr;

	return Errno(result_errno);
}


Libc::Fs::Open_dir_result Libc::Fs::open_dir(char const *path, int flags)
{
	if (!_vfs.dir_entry_exists(path)) return Errno { ENOENT };
	if (!_vfs.directory(path))        return Errno { ENOTDIR };

	Open_dir *result_od_ptr { };
	_monitor.monitor([&] {
		result_od_ptr = new (_kernel_heap) Open_dir(_vfs_env, path);
		return Fn::COMPLETE;
	});
	if (result_od_ptr)
		return *result_od_ptr;

	assert(false);
}


void Libc::Fs::destroy(Open_file &of)
{
	fsync(of);

	/* cancel and sync with blocking read */
	_monitor.monitor([&] {
		of.closing = true;
		if (of.blocking)
			return Fn::INCOMPLETE;

		if (of.handle.detach() == Vfs::File_handle::Detach_result::RETRY)
			return Fn::INCOMPLETE;

		return Fn::COMPLETE;
	});
	Genode::destroy(_kernel_heap, &of);
}


void Libc::Fs::destroy(Open_dir &od)
{
	Genode::destroy(_kernel_heap, &od);
}


struct Sync
{
	enum { INITIAL, TIMESTAMP_UPDATED, SYNCED } state { INITIAL };

	Genode::Vfs::File_handle &handle;
	Genode::Vfs::Timestamp   mtime { };

	struct Attr { bool update_mtime; };

	Sync(Genode::Vfs::File_handle &handle, Attr const attr,
	     Libc::Current_real_time &current_real_time)
	:
		handle(handle)
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
		case INITIAL:
			if (handle.update_mtime(mtime) == Genode::Vfs::Update_mtime_result::RETRY)
				return false;
			state = TIMESTAMP_UPDATED; [[ fallthrough ]];
		case TIMESTAMP_UPDATED:
			if (handle.sync() == Genode::Vfs::Sync_result::RETRY)
				return false;
			state = SYNCED; [[ fallthrough ]];
		case SYNCED:
			break;
		}
		return true;
	}
};


int Libc::Fs::fstat(Open_file &of, struct stat &buf)
{
	if (of.modified)
		fsync(of);

	return stat(of.handle.path.string(), buf);
}


int Libc::Fs::fstat(Open_dir &od, struct stat &buf)
{
	return stat(od.handle.path.string(), buf);
}


int Libc::Fs::mkdir(const char *path, mode_t mode)
{
	int result = -1;
	int result_errno = 0;

	Vfs::Timestamp const mtime = timestamp_now(*this);

	bool first = true;
	_monitor.monitor([&] {

		/* text for confict only once */
		if (first && _root_dir.entry_exists(path)) {
			result_errno = EEXIST;
			return Fn::COMPLETE;
		}
		first = false;

		Vfs::Mkdir_result mkdir_result = _vfs.mkdir(path, mtime);
		if (mkdir_result == Vfs::Mkdir_result::RETRY)
			return Fn::INCOMPLETE;

		if (mkdir_result == Vfs::Mkdir_result::CREATED
		 || mkdir_result == Vfs::Mkdir_result::UPDATED)
			result = 0;
		else
			result_errno = EPERM;

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

	Vfs::File_system::Stat stat;

	switch (_vfs.stat(path, stat)) {
	case Vfs::Stat_result::DENIED: errno = ENOENT; return -1;
	case Vfs::Stat_result::OK:     break;
	}

	vfs_stat_to_libc_stat_struct(stat, path, buf);
	return 0;
}


int Libc::Fs::stat(char const *path, struct stat &buf)
{
	if (!path)
		return Errno(EFAULT);

	Vfs::File_system::Stat stat;

	int result = -1;
	int result_errno = 0;
	_monitor.monitor([&] {
		switch (_vfs.stat(path, stat)) {
		case Vfs::Stat_result::DENIED: result_errno = ENOENT; break;
		case Vfs::Stat_result::OK:
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


ssize_t Libc::Fs::write(Open_file &of, const void *buf, ::size_t count, bool nonblock)
{
	Vfs::Write_result result = 0;

	if (nonblock) {
		_monitor.monitor([&] {
			Const_byte_range_ptr const src { (char const *)buf, count };
			result = of.handle.write({ .pos = of.pos }, src);
			result.with_result([&] (size_t n) { of.pos += n; },
			                   [&] (Vfs::Write_error) { });
			return Fn::COMPLETE;
		});
		if (result == Vfs::Write_error::RETRY)
			return Errno(EWOULDBLOCK);
	} else {
		size_t   total_written_bytes = 0;
		size_t   remaining_bytes = count;
		::off_t  offset = 0;
		unsigned iteration = 0;

		auto _fd_refers_to_continuous_file = [&]
		{
			Vfs::File_system::Stat stat { };

			if (_vfs.stat(of.handle.path.string(), stat) != Vfs::Stat_result::OK)
				return false;

			return stat.type == Vfs::Dirent_type::FILE;
		};

		_monitor.monitor([&] {

			Span const src { (char const *)buf + offset, remaining_bytes };
			Vfs::At const at { .pos = of.pos };

			Vfs::Write_result partial_result = of.handle.write(at, src);

			if (partial_result == Vfs::Write_error::RETRY)
				return Fn::INCOMPLETE;

			partial_result.with_result(
				[&] (size_t num_bytes) {
					total_written_bytes += num_bytes;
					offset              += num_bytes;
					remaining_bytes     -= num_bytes;

					of.pos     += num_bytes;
					of.modified = true;

					result = total_written_bytes;
				},
				[&] (Vfs::Write_error e) {
					result = e;
				});

			if (partial_result.failed())
				return Fn::COMPLETE;

			if (remaining_bytes == 0)
				return Fn::COMPLETE;

			/*
			 * If the write has not consumed all bytes, set up another partial
			 * write iteration with the remaining bytes as 'count'.
			 *
			 * The costly 'fd_refers_to_continuous_file' is called for the
			 * first iteration only.
			 */
			bool const continuous_file = (iteration > 0 || _fd_refers_to_continuous_file());

			if (!continuous_file) {
				warning("partial write on transactional file");
				result = Vfs::Write_error::DENIED;
				return Fn::COMPLETE;
			}
			iteration++;
			return Fn::INCOMPLETE;
		});
	}

	return result.convert<ssize_t>(
		[&] (size_t num_bytes)   -> ssize_t { return num_bytes; },
		[&] (Vfs::Write_error e) -> ssize_t { return Errno(EINVAL); });
}


ssize_t Libc::Fs::read(Open_file &of, void *buf, ::size_t count, bool nonblock)
{
	using Result = Vfs::Read_result;

	int result_errno = 0;
	size_t out_count = 0;
	bool queued = false;

	_monitor.monitor([&] {

		if (of.closing) result_errno = EBADF;

		if (!queued && nonblock && (of.handle.read_ready() != Vfs::Read_ready_result::YES))
			result_errno = EAGAIN;

		if (result_errno) {
			of.blocking = false;
			return Fn::COMPLETE;
		}

		of.blocking = true; /* sync with close */

		Byte_range_ptr const dst { (char *)buf, count };

		Result result = of.handle.read({ .pos = of.pos }, dst);
		if (result == Vfs::Read_error::RETRY) {
			queued = true;
			return Fn::INCOMPLETE; /* keep blocking */
		}

		of.blocking = false;

		result.with_result(
			[&] (size_t num_bytes) {
				of.pos += num_bytes;
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

	/* limit batch size to constrain array allocation on the stack */
	unsigned const max_entries = min(16ul, nbytes/sizeof(struct dirent));

	Vfs::Dirent vfs_dirents[max_entries] { };
	Vfs::Read_dir_result result = Vfs::Read_dir_error::DENIED;

	_monitor.monitor([&] {

		result = od.handle.read_dir({ .pos = od.pos }, { vfs_dirents, max_entries });

		return result == Vfs::Read_dir_error::RETRY ? Fn::INCOMPLETE : Fn::COMPLETE;
	});

	/*
	 * Convert dirent structure from VFS to libc
	 */
	auto convert_entry = [&] (Vfs::Dirent const &src, dirent &dst)
	{
		auto dirent_type = [] (Vfs::Dirent_type type)
		{
			switch (type) {
			case Vfs::Dirent_type::DIRECTORY: return DT_DIR;
			case Vfs::Dirent_type::FILE:      return DT_REG;
			case Vfs::Dirent_type::DEVICE:    return DT_CHR;
			case Vfs::Dirent_type::SYMLINK:   return DT_LNK;
			}
			return DT_UNKNOWN;
		};

		Open_dir::Path const entry_path { od.handle.path, "/", Cstring(dst.d_name) };

		dst = { };
		Genode::copy_cstring(dst.d_name, src.name.buf, sizeof(dst.d_name));
		dst.d_type   = dirent_type(src.type);
		dst.d_fileno = pseudo_inode_from_path(entry_path.string());
		dst.d_reclen = sizeof(struct dirent);
		dst.d_namlen = Genode::strlen(dst.d_name);
	};

	return result.convert<ssize_t>(
		[&] (unsigned num_entries) {

			for (unsigned i = 0; i < num_entries; i++)
				convert_entry(vfs_dirents[i], *(dirent *)(buf + i*sizeof(dirent)));

			od.pos += num_entries;
			*basep += num_entries*sizeof(struct dirent);

			return ssize_t(num_entries*sizeof(struct dirent));
		},
		[&] (Vfs::Read_dir_error) { return 0; });
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

		if (fs.directory_exists(fd.ioctl_dir().string())) {
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


void Libc::Fs::lseek_from_kernel(File_descriptor &fd, ::off_t pos)
{
	if (!fd.open_file_ptr) {
		error("lseek_from_kernel called for non-file descriptor");
		return;
	}

	fd.open_file_ptr->pos = pos;
}


::off_t Libc::Fs::lseek(Open_file &of, ::off_t offset, int whence)
{
	switch (whence) {
	case SEEK_SET: of.pos  = offset; break;
	case SEEK_CUR: of.pos += offset; break;
	case SEEK_END:
		{
			struct stat stat;
			::memset(&stat, 0, sizeof(stat));
			fstat(of, stat);
			of.pos = stat.st_size + offset;
		}
		break;
	}
	return of.pos;
}


::off_t Libc::Fs::lseek(Open_dir &od, ::off_t offset, int whence)
{
	switch (whence) {
	case SEEK_SET: od.pos  = offset; break;
	case SEEK_CUR: od.pos += offset; break;
	case SEEK_END:
		{
			struct stat stat;
			::memset(&stat, 0, sizeof(stat));
			fstat(od, stat);
			od.pos = stat.st_size + offset;
		}
		break;
	}
	return od.pos;
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

		switch (of.handle.resize(length)) {

		case Vfs::Resize_result::OK:     succeeded    = true;  break;
		case Vfs::Resize_result::DENIED: result_errno = EPERM; break;

		case Vfs::Resize_result::OUT_OF_RAM:  /* never occurs, using vfs heap */
		case Vfs::Resize_result::OUT_OF_CAPS:

		case Vfs::Resize_result::RETRY: return Fn::INCOMPLETE;
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
	Vfs::Timestamp const mtime = timestamp_now(*this);

	bool succeeded    = false;
	int  result_errno = 0;

	_monitor.monitor([&] {

		switch (_vfs.symlink(link_path, target_path, mtime)) {
		case Vfs::Symlink_result::RETRY:   return Fn::INCOMPLETE;
		case Vfs::Symlink_result::CREATED: succeeded = true;     break;
		case Vfs::Symlink_result::UPDATED: succeeded = true;     break;
		case Vfs::Symlink_result::DENIED:  result_errno = EPERM; break;
		}
		return Fn::COMPLETE;
	});

	if (!succeeded)
		return Errno(result_errno);

	return 0;
}


ssize_t Libc::Fs::readlink(const char *link_path, char *buf, ::size_t buf_size)
{
	::size_t out_count    = 0;
	bool     succeeded    = false;
	int      result_errno = 0;

	Byte_range_ptr dst(buf, buf_size);

	_monitor.monitor([&] {

		Vfs::Follow_result const result = _vfs.follow(link_path, dst);

		if (result == Vfs::Follow_error::RETRY)
			return Fn::INCOMPLETE;

		result.with_result(
			[&] (Vfs::Path_elem const path_elem) {
				out_count = ::strlen(buf);
				if (path_elem.last(Span::from_cstring(link_path)))
					succeeded = true;
				else
					result_errno = ENOENT;
			},
			[&] (Vfs::Follow_error) { result_errno = ENOENT; }
		);
		return Fn::COMPLETE;
	});

	if (!succeeded)
		return Errno(result_errno);

	return out_count;
}


int Libc::Fs::unlink(char const *path)
{
	bool succeeded = false;
	int result_errno = 0;
	_monitor.monitor([&] {
		switch (_vfs.unlink(path)) {
		case Vfs::Unlink_result::DENIED: result_errno = ENOENT; break;
		case Vfs::Unlink_result::OK:     succeeded = true;      break;
		}
		return Fn::COMPLETE;
	});
	if (!succeeded)
		return Errno(result_errno);

	return 0;
}


int Libc::Fs::rename(char const *from_path, char const *to_path)
{
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
		case Vfs::Rename_result::DENIED: result_errno = ENOENT; break;
		case Vfs::Rename_result::OK:     succeeded = true;      break;
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

	Open_file &reference_of;

	Mmap_entry(Registry<Mmap_entry> &registry, void *start, Open_file &reference_of)
	:
		Registry<Mmap_entry>::Element(registry, *this), start(start),
		reference_of(reference_of)
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
		 *     'Vfs::File_system::dataspace'.
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

		Open_file *reference_of_ptr = open_file({
			.path      = fd.path,
			.writeable = (fd.flags & O_ACCMODE) != O_RDONLY
		}).convert<Open_file *>(
			[&] (Open_file &of) { return &of; },
			[&] (Errno) { return nullptr; });

		if (!reference_of_ptr) {
			error("mmap could not create reference file handle");
			errno = ENFILE;
			return MAP_FAILED;
		}

		Dataspace_capability ds_cap;

		_monitor.monitor([&] {
			ds_cap = _vfs.dataspace(fd.path.string());
			return Fn::COMPLETE;
		});

		auto drop_reference_handle = [&]
		{
			if (reference_of_ptr)
				destroy(*reference_of_ptr);
			reference_of_ptr = nullptr;
		};

		if (!ds_cap.valid()) {
			error("mmap got invalid dataspace capability");
			drop_reference_handle();
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
			drop_reference_handle();
			errno = ENOMEM;
			return MAP_FAILED;
		}

		new (_kernel_heap) Mmap_entry(mmap_registry(), addr, *reference_of_ptr);
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

	Open_file *reference_of_ptr = nullptr;

	mmap_registry().for_each([&] (Mmap_entry &entry) {
		if (entry.start == addr) {
			reference_of_ptr = &entry.reference_of;
			Genode::destroy(_kernel_heap, &entry);
			_local_rm.detach(addr_t(addr));
		}
	});

	if (!reference_of_ptr)
		return Errno(EINVAL);

	destroy(*reference_of_ptr);
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
				if (of.handle.read_ready() == Vfs::Read_ready_result::YES) {
					*fds[pollfd_index].revents |= POLLIN;
					fd_ready = true;
				}
			}

			if (fds[pollfd_index].events & (POLLOUT | POLLWRNORM | POLLWRBAND)) {
				if (of.handle.write_ready() == Vfs::Write_ready_result::YES) {
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
	using Open_file  = Libc::Open_file;
	using Result     = Genode::Vfs::Read_result;

	bool progress = false;

	aio_job.with_aio_handle([&] (Aio_handle &aio_handle) {
		aio_handle.with_open_file([&] (Open_file &of) {

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
				Genode::Byte_range_ptr const dst {
					(char *)aio_job.iocb->aio_buf, aio_job.iocb->aio_nbytes };
				Genode::Vfs::At const at { .pos = uint64_t(aio_job.iocb->aio_offset) };

				Result const result = of.handle.read(at, dst);
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
	using namespace Libc;
	using Aio_job    = File_descriptor::Aio_job;
	using Aio_handle = File_descriptor::Aio_handle;

	bool progress = false;

	aio_job.with_aio_handle([&] (Aio_handle &aio_handle) {
		aio_handle.with_open_file([&] (Open_file &of) {

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

				of.pos = aio_job.iocb->aio_offset;

				aio_handle.count = aio_job.iocb->aio_nbytes;
				aio_handle.offset = 0;

				aio_handle.state = Aio_handle::State::QUEUED;
				progress = true;
				break;
			}
			case Aio_handle::State::QUEUED:
			{
				Const_byte_range_ptr const src {
					(char *)aio_job.iocb->aio_buf + aio_handle.offset, aio_handle.count };

				of.handle.write({ .pos = of.pos }, src).with_result(
					[&] (size_t num_bytes) {
						aio_handle.count  -= num_bytes;
						aio_handle.offset += num_bytes;
						aio_job.result    += num_bytes;
						of.pos            += num_bytes;

						if (!aio_handle.count)
							aio_handle.state = Aio_handle::State::COMPLETE;

						progress = true;
					},
					[&] (Vfs::Write_error e) {
						switch (e) {
						case Vfs::Write_error::DENIED:
							aio_job.result = -1;
							aio_job.error  = EINVAL;
							aio_handle.state = Aio_handle::State::COMPLETE;
							progress = true;
							break;
						case Vfs::Write_error::RETRY:       break;
						case Vfs::Write_error::OUT_OF_RAM:  break; /* never */
						case Vfs::Write_error::OUT_OF_CAPS: break; /* never */
						}
					});
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

	if (!fd.open_file_ptr) {
		error("wait_aio called for non-file fd (", fd.path, ")");
		return Errno { EBADF };
	}

	fd.for_each_aio_job(Aio_job::State::PENDING, [&] (Aio_job &aio_job) {
		fd.any_unused_aio_handle([&] (Aio_handle &aio_handle) {

			if (aio_handle.of_ptr == nullptr) {
				aio_handle.of_ptr =
					open_file({
						.path      = fd.path.string(),
						.writeable = (fd.flags & O_ACCMODE) != O_RDONLY
					}).convert<Open_file *>(
						[&] (Open_file &of)        { return &of; },
						[&] (Errno) -> Open_file * { return nullptr; });

				/*
				 * This should not happen and at this point we bail
				 * alltogether for now.
				 */
				if (aio_handle.of_ptr == nullptr) {
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


bool Libc::Fs::directory_exists(char const *path) const
{
	bool result;
	_monitor.monitor([&] {
		result = _root_dir.directory_exists(path);
		return Fn::COMPLETE;
	});
	return result;
}


void Libc::Fs::_with_file_content(char const *path, With_file_content::Ft const &fn) const
{
	_monitor.monitor([&] {
		File_content content { _kernel_heap, _root_dir, path, File_content::Limit { 4096 } };
		content.bytes([&] (char const *ptr, size_t size) { fn(Span { ptr, size }); });
		return Fn::COMPLETE;
	});
}
