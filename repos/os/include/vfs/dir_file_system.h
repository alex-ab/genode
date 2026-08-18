/*
 * \brief  Directory file system
 * \author Norman Feske
 * \date   2026-08-16
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__DIR_FILE_SYSTEM_H_
#define _INCLUDE__VFS__DIR_FILE_SYSTEM_H_

#include <base/registry.h>
#include <vfs/union_file_system.h>
#include <vfs/vfs_handle.h>

namespace Genode::Vfs { class Dir_file_system; }


class Genode::Vfs::Dir_file_system : public File_system, public Parent_fs
{
	public:

		/**
		 * Directory name
		 */
		enum { MAX_NAME_LEN = 128 };
		using Name = String<MAX_NAME_LEN>;

	private:

		Vfs::Env &_env;

		Parent_fs &_parent_fs;

		Name const _name;

		Union_file_system _union;

		bool _slash(char const *path) const { return strcmp(path, "/") == 0; }

		/**
		 * Call 'fn' with the portion of the path following the dir name,
		 * or 'mismatch_fn' if the path is unrelated to this directory.
		 */
		auto _with_sub_path(char const *path, auto const &fn, auto const &mismatch_fn)
		-> decltype(mismatch_fn())
		{
			/* skip heading slash in path if present */
			if (path[0] == '/')
				path++;

			size_t const name_len = strlen(_name.string());
			if (strcmp(path, _name.string(), name_len) != 0)
				return mismatch_fn();

			path += name_len;

			/*
			 * The first characters of the first path element are equal to
			 * the current directory name. Let's check if the length of the
			 * first path element matches the name length.
			 */
			if (*path != 0 && *path != '/')
				return mismatch_fn();

			return fn(path);
		}

		auto _with_sub_dir_path(char const *path, auto const &fn, auto const &mismatch_fn)
		-> decltype(mismatch_fn())
		{
			return _with_sub_path(path,
				[&] (char const *path) {
					if (strlen(path) == 0)
						path = "/"; /* ensure invariant of leading slash */
					return fn(path);
				}, mismatch_fn);
		}

		struct Dir_vfs_handle : Vfs_handle
		{
			Dir_file_system &_fs;

			Dir_vfs_handle(Dir_file_system &fs, Allocator &alloc)
			:
				Vfs_handle(fs, alloc, 0), _fs(fs)
			{ }

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (dst.num_bytes < sizeof(Dirent))
					return Read_error::DENIED;

				file_size const index = at.pos / sizeof(Dirent);

				Dirent &dirent = *(Dirent*)dst.start;

				if (index == 0) {
					dirent = {
						.type = Dirent_type::DIRECTORY,
						.rwx  = Node_rwx::rwx(),
						.name = { _fs._name.string() }
					};
				} else {
					dirent = {
						.type = Dirent_type::END,
						.rwx  = { },
						.name = { }
					};
				}
				return sizeof(Dirent);
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return false; }
		};

	protected:

		/**
		 * Parent_fs role for '_union' child of this dir file system
		 */
		void notify_watchers(Span const &rel_path) override
		{
			using Path = String<MAX_PATH_LEN>;
			Path { "/", _name, Cstring(rel_path.start, rel_path.num_bytes) }
				.with_span([&] (Span const &s) {
					_parent_fs.notify_watchers(s); });
		}

	public:

		Dir_file_system(Env &env, Parent_fs &parent_fs, Name const &name)
		:
			_env(env), _parent_fs(parent_fs), _name(name), _union(env, *this)
		{ }

		Dir_file_system(Env &env, Parent_fs &parent_fs, Node const &node)
		:
			Dir_file_system(env, parent_fs, node.attribute_value("name", Name()))
		{ }

		Dataspace_capability dataspace(char const *path) override
		{
			return _with_sub_path(path,
				[&] (auto const &path) { return _union.dataspace(path); },
				[&]                    { return Dataspace_capability(); });
		}

		void release(char const *path, Dataspace_capability ds_cap) override
		{
			_with_sub_path(path,
				[&] (auto const &path) { _union.release(path, ds_cap); },
				[&]                    { });
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			if (_slash(path)) {
				out = {
					.size              = 0,
					.type              = Node_type::DIRECTORY,
					.rwx               = Node_rwx::rwx(),
					.device            = (addr_t)this,
					.modification_time = { },
				};
				return STAT_OK;
			}
			return _with_sub_path(path,
				[&] (auto const &path) { return _union.stat(path, out); },
				[&] () -> Stat_result  { return STAT_ERR_NO_ENTRY; });
		}

		unsigned num_dirent(char const *path) override
		{
			if (_slash(path))
				return 1;

			return _with_sub_dir_path(path,
				[&] (auto const &path) { return _union.num_dirent(path); },
				[&] () -> unsigned     { return 0; });
		}

		bool directory(char const *path) override
		{
			if (_slash(path))
				return true;

			return _with_sub_path(path,
				[&] (auto const &path) { return _union.directory(path); },
				[&] () -> bool         { return false; });
		}

		bool dir_entry_exists(char const *path) override
		{
			if (_slash(path))
				return true;

			return _with_sub_path(path,
				[&] (auto const &path) { return _union.dir_entry_exists(path); },
				[&] () -> bool         { return false; });
		}

		Open_result open(char const *path, unsigned mode,
		                 Vfs_handle **out, Allocator &alloc) override
		{
			if (_slash(path))
				return OPEN_ERR_NO_PERM; /* cannot open dir as file */

			return _with_sub_path(path,
				[&] (auto const &path) {
					return _union.open(path, mode, out, alloc);
				},
				[&] () -> Open_result { return OPEN_ERR_UNACCESSIBLE; });
		}

		Opendir_result opendir(char const *path, bool create,
		                       Vfs_handle **out, Allocator &alloc) override
		{
			if (_slash(path)) {
				if (create)
					return OPENDIR_ERR_PERMISSION_DENIED;

				try { *out = new (alloc) Dir_vfs_handle(*this, alloc); }
				catch (Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM; }
				catch (Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }
				return OPENDIR_OK;
			}

			return _with_sub_dir_path(path,
				[&] (char const *path) {
					return _union.opendir(path, create, out, alloc);
				},
				[&] () -> Opendir_result { return OPENDIR_ERR_LOOKUP_FAILED; });
		}

		Openlink_result openlink(char const *path, bool create,
		                         Vfs_handle **out, Allocator &alloc) override
		{
			if (_slash(path))
				return OPENLINK_ERR_PERMISSION_DENIED; /* cannot open dir as link */

			return _with_sub_path(path,
				[&] (auto const &path) {
					return _union.openlink(path, create, out, alloc);
				},
				[&] () -> Openlink_result { return OPENLINK_ERR_LOOKUP_FAILED; });
		}

		void close(Vfs_handle *handle) override
		{
			if (handle && (&handle->ds() == this))
				destroy(handle->alloc(), handle);
		}

		Watch_result watch(char const *path) override
		{
			return _with_sub_path(path,
				[&] (auto const &path) { return _union.watch(path); },
				[&] () -> Watch_result { return Ok(); });
		}

		void unwatch(char const *path) override
		{
			_with_sub_path(path,
				[&] (auto const &path) { _union.unwatch(path); },
				[&]                    { });
		}

		Unlink_result unlink(char const *path) override
		{
			if (_slash(path))
				return UNLINK_ERR_NO_PERM;

			return _with_sub_path(path,
				[&] (auto const &path)  { return _union.unlink(path); },
				[&] () -> Unlink_result { return UNLINK_ERR_NO_ENTRY; });
		}

		Rename_result rename(char const *from_path, char const *to_path) override
		{
			/* deny renaming a path in the static VFS configuration */
			if (_slash(from_path))
				return RENAME_ERR_NO_PERM;

			return _with_sub_path(from_path,
				[&] (auto const &from_path) {
					return _with_sub_path(to_path,
						[&] (auto const &to_path) {
							return _union.rename(from_path, to_path);
						},
						[&] () -> Rename_result {
							/* both paths must reside within the same file system */
							return RENAME_ERR_CROSS_FS;
						});
				},
				[&] () -> Rename_result { return RENAME_ERR_NO_ENTRY; });
		}

		static char const *name()   { return "dir"; }
		char const *type() override { return "dir"; }

		void update(Node const &node, Factory &factory) override
		{
			_union.update(node, factory);
		}
};

#endif /* _INCLUDE__VFS__DIR_FILE_SYSTEM_H_ */
