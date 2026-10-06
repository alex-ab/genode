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
#include <vfs/env.h>
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

		struct Rump_dir_channel;
		struct Rump_watch_handle;
		using Rump_watch_handles = List<Rump_watch_handle>;
		Rump_watch_handles _watchers { };

		struct File_channel : Vfs::File_channel
		{
			Allocator   &_alloc;
			File_system &_fs;

			struct Attr
			{
				Path path;
				int  fd;
				bool new_dir_entry;
				bool writeable;
			};

			Attr const attr;

			bool modifying = false;

			File_channel(Allocator &alloc, File_system &fs, Attr attr)
			:
				Vfs::File_channel({ .writeable = attr.writeable }),
				_alloc(alloc), _fs(fs), attr(attr)
			{ }

			~File_channel()
			{
				rump_sys_close(attr.fd);
				if (attr.new_dir_entry)
					_fs._notify_compound_dir_watchers(attr.path.base());
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }

			Resize_result resize(file_size len)
			{
				if (rump_sys_ftruncate(attr.fd, len) != 0) switch (errno) {
				default:
					error(__func__, ": unhandled rump error ", errno);
					[[fallthrough]];
				case EACCES:
				case EINTR:
				case ENOSPC:
					return Resize_result::DENIED;
				}
				modifying = true;
				return Resize_result::OK;
			}

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				ssize_t n = rump_sys_pread(attr.fd, dst.start, dst.num_bytes, at.pos);
				if (n >= 0)
					return n;

				switch (errno) {
				case EWOULDBLOCK:
				case EINVAL:
				case EIO:
				case EINTR:
					break;
				default:
					error(__func__, ": expected rump error ", errno, " during read");
				}
				return Read_error::DENIED;
			}

			Write_result write(At const at, Const_byte_range_ptr const &src) override
			{
				ssize_t n = rump_sys_pwrite(attr.fd, src.start, src.num_bytes, at.pos);
				if (n == -1) switch (errno) {
				case EWOULDBLOCK:
					return Write_error::RETRY;

				case EINVAL:
				case EIO:
				case EINTR:
					return Write_error::DENIED;

				default:
					error(__func__, ": unhandled rump error ", errno);
					return Write_error::DENIED;
				}
				modifying = true;
				return n;
			}

			Sync_result sync() override
			{
				_rump_sync();
				if (modifying) {
					_fs._notify_watchers(attr.path.base());
					modifying = false;
				}
				if (attr.new_dir_entry)
					_fs._notify_compound_dir_watchers(attr.path.base());
				return Sync_result::OK;
			}

			Update_mtime_result update_mtime(Timestamp time) override
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
				return Update_mtime_result::OK;
			}

			void destruct() override { destroy(_alloc, this); }
		};

		struct Rump_dir_channel : Vfs::Dir_channel
		{
			Allocator &_alloc;

			struct Attr
			{
				Path path;
				int  fd;
			};

			Attr const _attr;

			Read_dir_result _finish_read(char const *path,
			                             struct ::dirent *dent, Dirent &vfs_dir)
			{
				/*
				 * We cannot use 'd_type' member of 'dirent' here since the EXT2
				 * implementation sets the type to unkown. Hence we use stat.
				 */
				struct stat s { };
				rump_sys_lstat(path, &s);

				bool const valid = (S_ISREG (s.st_mode))
				                || (S_ISDIR (s.st_mode))
				                || (S_ISLNK (s.st_mode))
				                || (S_ISBLK (s.st_mode))
				                || (S_ISCHR (s.st_mode))
				                || (S_ISFIFO(s.st_mode));

				if (!valid)
					return Read_dir_end();

				auto dirent_type = [] (unsigned mode)
				{
					if (S_ISDIR (mode)) return Dirent_type::DIRECTORY;
					if (S_ISLNK (mode)) return Dirent_type::SYMLINK;
					else                return Dirent_type::FILE;
				};

				Node_rwx const rwx { .readable   = true,
				                     .writeable  = true,
				                     .executable = (s.st_mode & S_IXUSR) != 0 };

				vfs_dir = {
					.type = dirent_type(s.st_mode),
					.rwx  = rwx,
					.name = { dent->d_name }
				};
				return 1;
			}

			Rump_dir_channel(Allocator &alloc, Attr attr)
			:
				_alloc(alloc), _attr(attr)
			{ }

			~Rump_dir_channel() { rump_sys_close(_attr.fd); }

			void destruct() override { destroy(_alloc, this); }

			Read_dir_result read_dir(At const at, Dirent_range_ptr const &dst) override
			{
				if (dst.num_entries < 1)
					return Read_dir_error::DENIED;

				rump_sys_lseek(_attr.fd, 0, SEEK_SET);

				int bytes;
				unsigned fileno = 0;
				char *buf  = _buffer();
				struct ::dirent *dent = nullptr;
				do {
					bytes = rump_sys_getdents(_attr.fd, buf, BUFFER_SIZE);
					void *current, *end;
					for (current = buf, end = &buf[bytes];
					     current < end;
					     current = _DIRENT_NEXT((::dirent *)current))
					{
						dent = (::dirent *)current;
						if (strcmp(".", dent->d_name) && strcmp("..", dent->d_name)) {
							if (fileno++ == at.pos) {
								Path newpath(dent->d_name, _attr.path.base());
								return _finish_read(newpath.base(), dent, dst.start[0]);
							}
						}
					}
				} while (bytes > 0);

				return Read_dir_end();
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
		:
			Vfs::File_system(Ident::from_node(config)),
			_env(env), _parent_fs(parent_fs)
		{
			using Fs_type = String<16>;

			Fs_type fs_type = config.attribute_value("fs", Fs_type());

			if (!_check_type(fs_type.string())) {
				error("Invalid or no file system given (use 'rump | fs: <fs-type>')");
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

		void destruct() override { destroy(_env.alloc(), this); }

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

		unsigned num_dirent(char const *path) override
		{
			unsigned n = 0;
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

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
		{
			unsigned mode = attr.writeable ? O_RDWR : O_RDONLY;

			if (attr.create)
				mode |= O_CREAT;

			bool const new_dir_entry = attr.create && !dir_entry_exists(path);

			enum { DEFAULT_PERMISSIONS = 0777 };
			int const fd = attr.create ? rump_sys_open(path, mode, DEFAULT_PERMISSIONS)
			                           : rump_sys_open(path, mode);

			if (fd == -1) switch (errno) {
			default:
				error(__func__, ": unhandled rump error ", errno);
				[[fallthrough]];
			case ENAMETOOLONG:
			case EACCES:
			case ENOENT:
			case EEXIST:
			case ENOSPC:
				return Open_error::DENIED;
			}

			Open_error error = Open_error::DENIED;
			try {
				File_channel &c = *new (alloc)
					File_channel(alloc, *this, {
						.path          = { path },
						.fd            = fd,
						.new_dir_entry = new_dir_entry,
						.writeable     = attr.writeable
					});
				if (attr.create) c.modifying = true;
				return c;
			}
			catch (Out_of_ram)  { error = Open_error::OUT_OF_RAM;  }
			catch (Out_of_caps) { error = Open_error::OUT_OF_CAPS; }
			rump_sys_close(fd);
			return error;
		}

		Opendir_result opendir(char const *path, Allocator &alloc) override
		{
			if (strlen(path) == 0)
				path = "/";

			int const fd = rump_sys_open(path, O_RDONLY | O_DIRECTORY);
			if (fd == -1) {
				switch (errno) {
				default:
					error(__func__, ": unhandled rump error ", errno);
					[[fallthrough]];
				case ENAMETOOLONG:
				case EACCES:
				case ENOENT:
				case EEXIST:
				case ENOSPC:
					break;
				}
				return Opendir_error::DENIED;
			}

			Opendir_error error = Opendir_error::DENIED;
			try {
				return *new (alloc) Rump_dir_channel(alloc, { .path = { path },
				                                              .fd   = fd });
			}
			catch (Out_of_ram)  { error = Opendir_error::OUT_OF_RAM;  }
			catch (Out_of_caps) { error = Opendir_error::OUT_OF_CAPS; }

			if (error != Opendir_error::DENIED)
				rump_sys_close(fd);

			return error;
		}

		Stat_result stat(char const *path, Stat &stat)
		{
			struct stat sb { };
			if (rump_sys_lstat(path, &sb) != 0) return Stat_result::DENIED;

			auto type = [] (unsigned mode)
			{
				if (S_ISDIR(mode)) return Dirent_type::DIRECTORY;
				if (S_ISLNK(mode)) return Dirent_type::SYMLINK;

				return Dirent_type::FILE;
			};

			stat = {
				.size   = (file_size)sb.st_size,
				.type   = type(sb.st_mode),
				.rwx    = { .readable   = true,
				            .writeable  = true,
				            .executable = (sb.st_mode & S_IXUSR) != 0 },
				.modification_time = {
					.ms_since_1970 = uint64_t(sb.st_mtim.tv_sec*1000 +
					                          sb.st_mtim.tv_nsec/1000000) }
			};

			return Stat_result::OK;
		}

		Unlink_result unlink(char const *path) override
		{
			struct stat s;
			if (rump_sys_lstat(path, &s) == -1)
				return Unlink_result::DENIED;

			int const r = S_ISDIR(s.st_mode)
				? rump_sys_rmdir(path)
				: rump_sys_unlink(path);

			if (r != 0) switch (errno) {
			default:
				error(__func__, ": unhandled rump error ", errno);
				[[fallthrough]];
			case ENOENT:
			case ENOTEMPTY:
				return Unlink_result::DENIED;
			}

			_notify_watchers(path);
			_notify_compound_dir_watchers(path);

			return Unlink_result::OK;
		}

		Rename_result rename(char const *from, char const *to) override
		{
			if (rump_sys_rename(from, to) != 0) switch (errno) {
			case ENOENT:
			case EXDEV:
			case EACCES:
				return Rename_result::DENIED;
			}

			_notify_watchers(from);
			_notify_watchers(to);
			_notify_compound_dir_watchers(from);
			_notify_compound_dir_watchers(to);

			return Rename_result::OK;
		}

		Mkdir_result mkdir(char const *path, Timestamp) override
		{
			if (strlen(path) == 0)
				path = "/";

			bool const new_dir_entry = !dir_entry_exists(path);

			if (new_dir_entry) {
				if (rump_sys_mkdir(path, 0777) != 0) switch (::errno) {
				default:
					error(__func__, ": unhandled rump error ", errno);
					[[fallthrough]];
				case ENAMETOOLONG:
				case EACCES:
				case ENOENT:
				case EEXIST:
				case ENOSPC:
					return Mkdir_result::DENIED;
				}
			}
			_notify_watchers(path);
			_notify_compound_dir_watchers(path);
			return new_dir_entry ? Mkdir_result::CREATED : Mkdir_result::UPDATED;
		}

		Symlink_result symlink(char const *path, char const *target, Timestamp) override
		{
			if (strlen(path) == 0)
				path = "/";

			bool const new_dir_entry = !dir_entry_exists(path);

			if (!new_dir_entry) {
				struct stat s { };
				rump_sys_lstat(path, &s);
				if (!S_ISLNK(s.st_mode))
					return Symlink_result::DENIED;

				rump_sys_unlink(path);
			}

			if (rump_sys_symlink(target, path) != 0)
				return Symlink_result::DENIED;

			_parent_fs.notify_watchers(Span::from_cstring(path));
			if (new_dir_entry)
				_notify_compound_dir_watchers(path);

			return new_dir_entry ? Symlink_result::CREATED
			                     : Symlink_result::UPDATED;
		}

		Follow_result follow(char const *path, Byte_range_ptr const &dst) override
		{
			if (!dst.num_bytes) /* no space for null-termination */
				return Follow_error::DENIED;

			return follow_path(Span::from_cstring(path),
				[&] (auto const &partial_path) {
					struct stat s { };
					return rump_sys_lstat(partial_path.string(), &s) == 0
					    && S_ISLNK(s.st_mode);
				},
				[&] (Path_elem const elem, auto const &partial_path) -> Follow_result {

					ssize_t n = rump_sys_readlink(partial_path.string(),
					                              dst.start, dst.num_bytes - 1);
					if (n < 0)
						return Follow_error::DENIED;

					/* ensure null termination */
					dst.start[min(size_t(n), dst.num_bytes - 1)] = 0;
					return elem;
				});
		}
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	/*
	 * Constructed on the first call of 'create'.
	 */
	struct _Factory : Vfs::File_system::Factory
	{
		struct Rump_fs_user : Rump_fs_user_wakeup
		{
			Vfs::Env::User &_vfs_user;

			void wakeup_rump_fs_user() override { _vfs_user.wakeup_vfs_user(); }

			Rump_fs_user(Vfs::Env::User &vfs_user) : _vfs_user(vfs_user) { }

		} _rump_fs_user;

		_Factory(Env &env, Allocator &alloc, Vfs::Env::User &vfs_user, Node const &config)
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

		using Fs = Vfs_rump::File_system;

		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &parent_fs, Node const &config) override
		{
			return { *this, { *new (env.alloc()) Fs(env, parent_fs, config) } };
		}

		void _free(Instance &instance) override { instance.fs.destruct(); };
	};

	struct Extern_factory : Vfs::File_system::Factory
	{
		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &parent_fs, Node const &node) override
		{
			static _Factory factory(env.env(), env.alloc(), env.user(), node);
			return factory.create(env, parent_fs, node);
		}

		void _free(Instance &instance) override { /* never called */ };
	};

	static Extern_factory factory;
	return &factory;
}
