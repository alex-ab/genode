/*
 * \brief  Rump VFS plugin
 * \author Christian Helmuth
 * \author Sebastian Sumpf
 * \author Emery Hemingway
 * \date   2015-09-05
 */

/*
 * Copyright (C) 2014-2022 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <rump/env.h>
#include <rump_fs/fs.h>
#include <vfs/file_system_factory.h>
#include <vfs/vfs_handle.h>
#include <os/path.h>

extern "C" {
#include <sys/cdefs.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/event.h>
#include <sys/time.h>
#include <rump/rump.h>
#include <rump/rump_syscalls.h>
}

extern int errno;

namespace Vfs_rump {

	using namespace Genode;
	using namespace Genode::Vfs;

	using size_t = Genode::size_t; /* ambiguous with dde_rump/src/sys/sys/types.h */

	using vfs_fileno_t = unsigned long; /* VFS 'Dirent' attribute */
	using vfs_inode_t  = unsigned long; /* VFS 'Stat' attributes */
	using vfs_device_t = unsigned long;

	class File_system;
	class Factory;
}


static void _rump_sync()
{
	/* prevent nested calls into rump */
	if (rump_io_backend_blocked_for_io())
		return;

	/* sync through front-end */
	rump_sys_sync();

	/* sync Genode back-end */
	rump_io_backend_sync();
}

static char const *fs_types[] = { RUMP_MOUNT_CD9660, RUMP_MOUNT_EXT2FS,
                                  RUMP_MOUNT_FFS, RUMP_MOUNT_MSDOS,
                                  RUMP_MOUNT_NTFS, RUMP_MOUNT_UDF, 0 };

class Vfs_rump::File_system : public Vfs::File_system
{
	private:

		enum { BUFFER_SIZE = 4096 };

		using Path = Genode::Path<MAX_PATH_LEN>;

		Vfs::Env  &_env;
		Parent_fs &_parent_fs;

		struct Rump_vfs_dir_handle;
		struct Rump_watch_handle;
		using Rump_watch_handles = List<Rump_watch_handle>;
		Rump_watch_handles _watchers { };

		struct Rump_vfs_file_handle;
		using Rump_vfs_file_handles = List<Rump_vfs_file_handle>;
		Rump_vfs_file_handles _file_handles;

		struct Rump_vfs_handle : Vfs_handle
		{
			File_system &_fs;

			struct Attr
			{
				Path path;
				int  fd;
				bool new_dir_entry;
			};

			Attr const attr;

			Rump_vfs_handle(File_system &fs, Allocator &alloc, int flags, Attr attr)
			:
				Vfs_handle(fs, alloc, flags), _fs(fs), attr(attr)
			{ }

			~Rump_vfs_handle()
			{
				if (attr.new_dir_entry)
					_fs._notify_compound_dir_watchers(attr.path.base());
			}
		};

		struct Rump_vfs_file_handle : Rump_vfs_handle, Rump_vfs_file_handles::Element
		{
			File_system &_fs;

			bool modifying = false;

			Rump_vfs_file_handle(File_system &fs, Allocator &alloc, int flags, Attr attr)
			:
				Rump_vfs_handle(fs, alloc, flags, attr), _fs(fs)
			{ }

			~Rump_vfs_file_handle() { rump_sys_close(attr.fd); }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }

			Ftruncate_result ftruncate(file_size len)
			{
				if (rump_sys_ftruncate(attr.fd, len) != 0) switch (errno) {
				case EACCES: return FTRUNCATE_ERR_NO_PERM;
				case EINTR:  return FTRUNCATE_ERR_INTERRUPT;
				case ENOSPC: return FTRUNCATE_ERR_NO_SPACE;
				default:
					error(__func__, ": unhandled rump error ", errno);
					return FTRUNCATE_ERR_NO_PERM;
				}
				modifying = true;
				return FTRUNCATE_OK;
			}

			Read_result complete_read(Byte_range_ptr const &dst, size_t &out_count) override
			{
				ssize_t n = rump_sys_pread(attr.fd, dst.start, dst.num_bytes, seek());
				if (n == -1) switch (errno) {
				case EWOULDBLOCK: return READ_ERR_WOULD_BLOCK;
				case EINVAL:      return READ_ERR_INVALID;
				case EIO:         return READ_ERR_IO;
				case EINTR:       return READ_ERR_IO;
				default:
					error(__func__, ": unhandled rump error ", errno);
					return READ_ERR_IO;
				}
				out_count = n;
				return READ_OK;
			}

			Write_result write(Const_byte_range_ptr const &src, size_t &out_count) override
			{
				out_count = 0;

				ssize_t n = rump_sys_pwrite(attr.fd, src.start, src.num_bytes, seek());
				if (n == -1) switch (errno) {
				case EWOULDBLOCK: return WRITE_ERR_WOULD_BLOCK;
				case EINVAL:      return WRITE_ERR_INVALID;
				case EIO:         return WRITE_ERR_IO;
				case EINTR:       return WRITE_ERR_IO;
				default:
					error(__func__, ": unhandled rump error ", errno);
					return WRITE_ERR_IO;
				}
				modifying = true;
				out_count = n;
				return WRITE_OK;
			}

			Sync_result complete_sync() override
			{
				_rump_sync();
				if (modifying) {
					_fs._notify_watchers(attr.path.base());
					modifying = false;
				}
				if (attr.new_dir_entry)
					_fs._notify_compound_dir_watchers(attr.path.base());
				return SYNC_OK;
			}

			bool update_modification_timestamp(Timestamp time) override
			{
				struct timespec ts[2] = {
					{
						.tv_sec  = 0,
						.tv_nsec = 0
					}, {
						.tv_sec  = time_t( time.ms_since_1970 / 1000),
						.tv_nsec = long((time.ms_since_1970 % 1000)*1000*1000)
					}
				};

				/* silently igore error */
				rump_sys_futimens(attr.fd, (const timespec*)&ts);
				return true;
			}
		};

		struct Rump_vfs_dir_handle : Rump_vfs_handle
		{
			Read_result _finish_read(char const *path,
			                         struct ::dirent *dent, Dirent &vfs_dir)
			{
				/*
				 * We cannot use 'd_type' member of 'dirent' here since the EXT2
				 * implementation sets the type to unkown. Hence we use stat.
				 */
				struct stat s { };
				rump_sys_lstat(path, &s);

				auto dirent_type = [] (unsigned mode)
				{
					if (S_ISREG (mode)) return Dirent_type::CONTINUOUS_FILE;
					if (S_ISDIR (mode)) return Dirent_type::DIRECTORY;
					if (S_ISLNK (mode)) return Dirent_type::SYMLINK;
					if (S_ISBLK (mode)) return Dirent_type::CONTINUOUS_FILE;
					if (S_ISCHR (mode)) return Dirent_type::CONTINUOUS_FILE;
					if (S_ISFIFO(mode)) return Dirent_type::CONTINUOUS_FILE;

					return Dirent_type::END;
				};

				Node_rwx const rwx { .readable   = true,
				                     .writeable  = true,
				                     .executable = (s.st_mode & S_IXUSR) != 0 };

				vfs_dir = {
					.type = dirent_type(s.st_mode),
					.rwx  = rwx,
					.name = { dent->d_name }
				};
				return READ_OK;
			}

			Rump_vfs_dir_handle(File_system &fs, Allocator &alloc, int flags, Attr attr)
			:
				Rump_vfs_handle(fs, alloc, flags, attr)
			{ }

			~Rump_vfs_dir_handle() { rump_sys_close(attr.fd); }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return false; }

			Read_result complete_read(Byte_range_ptr const &dst, size_t &out_count) override
			{
				out_count = 0;

				if (dst.num_bytes < sizeof(Dirent))
					return READ_ERR_INVALID;

				size_t const index = size_t(seek() / sizeof(Dirent));

				Dirent *vfs_dir = (Dirent*)dst.start;

				out_count = sizeof(Dirent);

				rump_sys_lseek(attr.fd, 0, SEEK_SET);

				int bytes;
				unsigned fileno = 0;
				char *buf  = _buffer();
				struct ::dirent *dent = nullptr;
				do {
					bytes = rump_sys_getdents(attr.fd, buf, BUFFER_SIZE);
					void *current, *end;
					for (current = buf, end = &buf[bytes];
					     current < end;
					     current = _DIRENT_NEXT((::dirent *)current))
					{
						dent = (::dirent *)current;
						if (strcmp(".", dent->d_name) && strcmp("..", dent->d_name)) {
							if (fileno++ == index) {
								Path newpath(dent->d_name, attr.path.base());
								return _finish_read(newpath.base(), dent, *vfs_dir);
							}
						}
					}
				} while (bytes > 0);

				*vfs_dir = Dirent();
				return READ_OK;
			}
		};

		struct Rump_vfs_symlink_handle : Rump_vfs_handle
		{
			Rump_vfs_symlink_handle(File_system &fs, Allocator &alloc, int flags, Attr attr)
			:
				Rump_vfs_handle(fs, alloc, flags, attr)
			{ }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }

			Read_result complete_read(Byte_range_ptr const &dst, size_t &out_count) override
			{
				out_count = 0;

				if (seek() != 0) {
					/* partial read is not supported */
					return READ_ERR_INVALID;
				}

				ssize_t n = rump_sys_readlink(attr.path.base(), dst.start, dst.num_bytes);
				if (n == -1)
					return READ_ERR_IO;

				out_count = n;

				return READ_OK;
			}

			Write_result write(Const_byte_range_ptr const &src, size_t &out_count) override
			{
				rump_sys_unlink(attr.path.base());

				if (rump_sys_symlink(src.start, attr.path.base()) != 0) {
					out_count = 0;
					return WRITE_OK;
				}

				_fs._parent_fs.notify_watchers(Span::from_cstring(attr.path.base()));

				out_count = src.num_bytes;
				return WRITE_OK;
			}
		};

		/*
		 * Must fit 'struct msdosfs_args' and 'struct ufs_args'. Needed to
		 * pass mount flags the file-system drivers.
		 */
		struct fs_args
		{
			char *fspec;

			/* unused */
			struct export_args30 _pad1;
			uid_t                _uid;
			gid_t                _gid;
			mode_t               _mask;

			int flags;

			char _pad[164];
		};

		static bool _check_type(char const *type)
		{
			for (int i = 0; fs_types[i]; i++)
				if (!strcmp(type, fs_types[i]))
					return true;
			return false;
		}

		void _print_types()
		{
			error("fs types:");
			for (int i = 0; fs_types[i]; ++i)
				error("\t", fs_types[i]);
		}

		static char *_buffer()
		{
			/* buffer for directory entries */
			static char buf[BUFFER_SIZE];
			return buf;
		}

		void _notify_watchers(char const *path)
		{
			_parent_fs.notify_watchers(Span::from_cstring(path));
		}

		void _notify_compound_dir_watchers(char const *path)
		{
			with_compound_dir(Span::from_cstring(path), [&] (Span const &dir_path) {
				_parent_fs.notify_watchers(dir_path); });
		}

	public:

		File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &config)
		: _env(env), _parent_fs(parent_fs)
		{
			using Fs_type = String<16>;

			Fs_type fs_type = config.attribute_value("fs", Fs_type());

			if (!_check_type(fs_type.string())) {
				error("Invalid or no file system given (use \'<rump fs=\"<fs type>\"/>)");
				_print_types();
				throw Exception();
			}

			/* mount into extra-terrestrial-file system */
			struct fs_args args { };

			if (fs_type == "msdos" && config.attribute_value("gemdos", false)) {
				enum { MSDOSFSMNT_GEMDOSFS = 8 };
				args.flags |= MSDOSFSMNT_GEMDOSFS;
			}

			int opts = config.attribute_value("writeable", true) ?
				0 : RUMP_MNT_RDONLY;

			args.fspec =  (char *)GENODE_DEVICE;
			if (rump_sys_mount(fs_type.string(), "/", opts, &args, sizeof(args)) == -1) {
				error("Mounting '",fs_type,"' file system failed (",errno,")");
				throw Exception();
			}
		}

		/***************************
		 ** File_system interface **
		 ***************************/

		static char const *name()   { return "rump"; }
		char const *type() override { return "rump"; }


		/*********************************
		 ** Directory service interface **
		 *********************************/

		Dataspace_capability dataspace(char const *path) override
		{
			struct stat s { };
			if (rump_sys_lstat(path, &s) != 0)
				return { };

			auto read_file_content = [&path] (Region_map::Range const range) -> bool
			{
				int const fd = rump_sys_open(path, O_RDONLY);
				size_t i = 0; /* bytes read */
				if (fd >= 0) {
					while (i < range.num_bytes) {
						size_t  const CHUNK_SIZE = 16U << 10;
						ssize_t const n = rump_sys_read(fd, (void *)(range.start + i),
						                                min(range.num_bytes - i, CHUNK_SIZE));
						if (n <= 0)
							break;
						i += n;
					}
					rump_sys_close(fd);
				}
				return (i == range.num_bytes);
			};

			return _env.env().ram().try_alloc(s.st_size).convert<Dataspace_capability>(
				[&] (Ram::Allocation &allocation) {
					return _env.env().rm().attach(allocation.cap, {
						.size = { },  .offset     = { },  .use_at    = { },
						.at   = { },  .executable = { },  .writeable = true
					}).convert<Dataspace_capability>(
						[&] (Genode::Env::Local_rm::Attachment &attachment) -> Dataspace_capability {

							bool const complete = read_file_content({
								.start     = addr_t(attachment.ptr),
								.num_bytes = attachment.num_bytes });

							if (complete) {
								allocation.deallocate = false;
								attachment.deallocate = false;
								return allocation.cap;
							}
							error("rump failed to read content into VFS dataspace");
							return Dataspace_capability();
						},
						[&] (Genode::Env::Local_rm::Error) {
							return Dataspace_capability(); }
					);
				},
				[&] (Ram_allocator::Alloc_error) {
					error("rump failed to allocate VFS dataspace of size ", s.st_size);
					return Dataspace_capability(); }
			);
		}

		void release(char const *path,
		             Dataspace_capability ds_cap) override
		{
			if (ds_cap.valid())
				_env.env().ram().free(
					static_cap_cast<Ram_dataspace>(ds_cap));
		}

		file_size num_dirent(char const *path) override
		{
			file_size n = 0;
			int fd = rump_sys_open(*path ? path : "/", O_RDONLY | O_DIRECTORY);
			if (fd == -1)
				return 0;

			rump_sys_lseek(fd, 0, SEEK_SET);

			int   bytes = 0;
			char *buf = _buffer();
			do {
				bytes = rump_sys_getdents(fd, buf, BUFFER_SIZE);
				void *current, *end;
				for (current = buf, end = &buf[bytes];
				     current < end;
				     current = _DIRENT_NEXT((::dirent *)current))
				{
					struct ::dirent *dent = (::dirent *)current;
					if (strcmp(".", dent->d_name) && strcmp("..", dent->d_name))
						++n;
				}
			} while(bytes);

			rump_sys_close(fd);
			return n;
		}

		bool directory(char const *path) override
		{
			struct stat s;
			if (rump_sys_lstat(path, &s) != 0) return false;
			return S_ISDIR(s.st_mode);
		}

		bool dir_entry_exists(char const *path) override
		{
			struct stat s;
			return rump_sys_lstat(path, &s) == 0;
		}

		Open_result open(char const *path, unsigned mode,
		                 Vfs_handle **handle,
		                 Allocator  &alloc) override
		{
			/* OPEN_MODE_CREATE (or O_EXC) will not work */
			bool create = mode & OPEN_MODE_CREATE;
			if (create)
				mode |= O_CREAT;

			bool const new_dir_entry = create && !dir_entry_exists(path);

			enum { DEFAULT_PERMISSIONS = 0777 };
			int fd = create ? rump_sys_open(path, mode, DEFAULT_PERMISSIONS) : rump_sys_open(path, mode);
			if (fd == -1) switch (errno) {
			case ENAMETOOLONG: return OPEN_ERR_NAME_TOO_LONG;
			case EACCES:       return OPEN_ERR_NO_PERM;
			case ENOENT:       return OPEN_ERR_UNACCESSIBLE;
			case EEXIST:       return OPEN_ERR_EXISTS;
			case ENOSPC:       return OPEN_ERR_NO_SPACE;
			default:
				error(__func__, ": unhandled rump error ", errno);
				return OPEN_ERR_NO_PERM;
			}

			try {
				Rump_vfs_file_handle *h = new (alloc)
					Rump_vfs_file_handle(*this, alloc, mode, {
						.path          = { path },
						.fd            = fd,
						.new_dir_entry = new_dir_entry
					});
				if (create) h->modifying = true;
				*handle = h;
				return OPEN_OK;
			} catch (Out_of_ram) {
				rump_sys_close(fd);
				return OPEN_ERR_OUT_OF_RAM;
			} catch (Out_of_caps) {
				rump_sys_close(fd);
				return OPEN_ERR_OUT_OF_CAPS;
			}
		}

		Opendir_result opendir(char const *path, bool create,
		                       Vfs_handle **handle, Allocator &alloc) override
		{
			if (strlen(path) == 0)
				path = "/";

			bool const new_dir_entry = create && !dir_entry_exists(path);

			if (create) {
				if (rump_sys_mkdir(path, 0777) != 0) switch (::errno) {
				case ENAMETOOLONG: return OPENDIR_ERR_NAME_TOO_LONG;
				case EACCES:       return OPENDIR_ERR_PERMISSION_DENIED;
				case ENOENT:       return OPENDIR_ERR_LOOKUP_FAILED;
				case EEXIST:       return OPENDIR_ERR_NODE_ALREADY_EXISTS;
				case ENOSPC:       return OPENDIR_ERR_NO_SPACE;
				default:
					error(__func__, ": unhandled rump error ", errno);
					return OPENDIR_ERR_PERMISSION_DENIED;
				}
			}

			int fd = rump_sys_open(path, O_RDONLY | O_DIRECTORY);
			if (fd == -1) switch (errno) {
			case ENAMETOOLONG: return OPENDIR_ERR_NAME_TOO_LONG;
			case EACCES:       return OPENDIR_ERR_PERMISSION_DENIED;
			case ENOENT:       return OPENDIR_ERR_LOOKUP_FAILED;
			case EEXIST:       return OPENDIR_ERR_NODE_ALREADY_EXISTS;
			case ENOSPC:       return OPENDIR_ERR_NO_SPACE;
			default:
				error(__func__, ": unhandled rump error ", errno);
				return OPENDIR_ERR_PERMISSION_DENIED;
			}

			try {
				Rump_vfs_dir_handle *h = new (alloc)
					Rump_vfs_dir_handle(*this, alloc, 0777, {
						.path          = { path },
						.fd            = fd,
						.new_dir_entry = new_dir_entry
					});
				*handle = h;
				return OPENDIR_OK;
			} catch (Out_of_ram) {
				rump_sys_close(fd);
				return OPENDIR_ERR_OUT_OF_RAM;
			} catch (Out_of_caps) {
				rump_sys_close(fd);
				return OPENDIR_ERR_OUT_OF_CAPS;
			}
		}

		Openlink_result openlink(char const *path, bool create,
		                         Vfs_handle **handle, Allocator &alloc) override
		{
			bool const new_dir_entry = create && !dir_entry_exists(path);

			if (create) {
				if (rump_sys_symlink("", path) != 0) switch (errno) {
				case EEXIST:       return OPENLINK_ERR_NODE_ALREADY_EXISTS;
				case ENOENT:       return OPENLINK_ERR_LOOKUP_FAILED;
				case ENOSPC:       return OPENLINK_ERR_NO_SPACE;
				case EACCES:       return OPENLINK_ERR_PERMISSION_DENIED;
				case ENAMETOOLONG: return OPENLINK_ERR_NAME_TOO_LONG;
				default:
					error(__func__, ": unhandled rump error ", errno);
					return OPENLINK_ERR_PERMISSION_DENIED;
				}
			}

			char dummy;
			if (rump_sys_readlink(path, &dummy, sizeof(dummy)) == -1) switch(errno) {
				case ENOENT: return OPENLINK_ERR_LOOKUP_FAILED;
				default:
					error(__func__, ": unhandled rump error ", errno);
					return OPENLINK_ERR_PERMISSION_DENIED;
			}

			try {
				*handle = new (alloc) Rump_vfs_symlink_handle(*this, alloc, 0777, {
					.path          = { path },
					.fd            = -1,
					.new_dir_entry = new_dir_entry
				});
				return OPENLINK_OK;
			}
			catch (Out_of_ram) { return OPENLINK_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPENLINK_ERR_OUT_OF_CAPS; }
		}

		void close(Vfs_handle *vfs_handle) override
		{
			if (Rump_vfs_file_handle *handle =
				dynamic_cast<Rump_vfs_file_handle *>(vfs_handle))
			{
				_file_handles.remove(handle);
				destroy(vfs_handle->alloc(), handle);
			}
			else
			if (Rump_vfs_dir_handle *handle =
				dynamic_cast<Rump_vfs_dir_handle *>(vfs_handle))
			{
				destroy(vfs_handle->alloc(), handle);
			}
			else
			if (Rump_vfs_symlink_handle *handle =
				dynamic_cast<Rump_vfs_symlink_handle *>(vfs_handle))
			{
				destroy(vfs_handle->alloc(), handle);
			}
		}

		Stat_result stat(char const *path, Stat &stat)
		{
			struct stat sb { };
			if (rump_sys_lstat(path, &sb) != 0) return STAT_ERR_NO_ENTRY;

			auto type = [] (unsigned mode)
			{
				if (S_ISDIR(mode)) return Node_type::DIRECTORY;
				if (S_ISLNK(mode)) return Node_type::SYMLINK;

				return Node_type::CONTINUOUS_FILE;
			};

			stat = {
				.size   = (file_size)sb.st_size,
				.type   = type(sb.st_mode),
				.rwx    = { .readable   = true,
				            .writeable  = true,
				            .executable = (sb.st_mode & S_IXUSR) != 0 },
				.device = vfs_device_t(sb.st_dev),

				.modification_time = {
					.ms_since_1970 = uint64_t(sb.st_mtim.tv_sec*1000 +
					                          sb.st_mtim.tv_nsec/1000000) }
			};

			return STAT_OK;
		}

		Unlink_result unlink(char const *path) override
		{
			struct stat s;
			if (rump_sys_lstat(path, &s) == -1)
				return UNLINK_ERR_NO_ENTRY;

			int const r = S_ISDIR(s.st_mode)
				? rump_sys_rmdir(path)
				: rump_sys_unlink(path);

			if (r != 0) switch (errno) {
			case ENOENT:    return UNLINK_ERR_NO_ENTRY;
			case ENOTEMPTY: return UNLINK_ERR_NOT_EMPTY;
			default:
				error(__func__, ": unhandled rump error ", errno);
				return UNLINK_ERR_NO_PERM;
			}

			_notify_watchers(path);
			_notify_compound_dir_watchers(path);

			return UNLINK_OK;
		}

		Rename_result rename(char const *from, char const *to) override
		{
			if (rump_sys_rename(from, to) != 0) switch (errno) {
			case ENOENT: return RENAME_ERR_NO_ENTRY;
			case EXDEV:  return RENAME_ERR_CROSS_FS;
			case EACCES: return RENAME_ERR_NO_PERM;
			}

			_notify_watchers(from);
			_notify_watchers(to);
			_notify_compound_dir_watchers(from);
			_notify_compound_dir_watchers(to);

			return RENAME_OK;
		}
};


extern "C" Genode::Vfs::File_system_factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	/*
	 * Constructed on the first call of 'create'.
	 */
	struct Factory : Vfs::File_system_factory
	{
		struct Rump_fs_user : Rump_fs_user_wakeup
		{
			Vfs::Env::User &_vfs_user;

			void wakeup_rump_fs_user() override { _vfs_user.wakeup_vfs_user(); }

			Rump_fs_user(Vfs::Env::User &vfs_user) : _vfs_user(vfs_user) { }

		} _rump_fs_user;

		Factory(Env &env, Allocator &alloc, Vfs::Env::User &vfs_user, Node const &config)
		:
			_rump_fs_user(vfs_user)
		{
			Rump::construct_env(env);

			rump_io_backend_init(_rump_fs_user);

			/* limit RAM consumption */
			if (!config.has_attribute("ram")) {
				error("mandatory 'ram' attribute missing");
				throw Exception();
			}

			Number_of_bytes const memlimit =
				config.attribute_value("ram", Number_of_bytes(0));

			rump_set_memlimit(memlimit);

			/* start rump kernel */
			try         { rump_init(); }
			catch (...) { throw Exception(); }

			/* register block device */
			rump_pub_etfs_register(
				GENODE_DEVICE, GENODE_BLOCK_SESSION, RUMP_ETFS_BLK);

			/* set all bits but the stickies */
			rump_sys_umask(S_ISUID|S_ISGID|S_ISVTX);

			/* increase open file limit */
			struct rlimit rlim { ~0U, ~0U };
			if (rump_sys_getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
				rlim.rlim_cur = rlim.rlim_max;
				rump_sys_setrlimit(RLIMIT_NOFILE, &rlim);
			}
		}

		Vfs::File_system *create(Vfs::Env &env, Vfs::Parent_fs &parent_fs, Node const &config) override
		{
			return new (env.alloc()) Vfs_rump::File_system(env, parent_fs, config);
		}
	};

	struct Extern_factory : Vfs::File_system_factory
	{
		Vfs::File_system *create(Vfs::Env &env, Vfs::Parent_fs &parent_fs, Node const &node) override
		{
			static Factory factory(env.env(), env.alloc(), env.user(), node);
			return factory.create(env, parent_fs, node);
		}
	};

	static Extern_factory factory;
	return &factory;
}
