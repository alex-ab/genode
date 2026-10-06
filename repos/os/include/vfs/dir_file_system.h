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

namespace Genode::Vfs { class Dir_file_system; }


class Genode::Vfs::Dir_file_system : public File_system, public Parent_fs
{
	public:

		/**
		 * Directory name
		 */
		enum { MAX_NAME_LEN = 128 };
		using Name = String<MAX_NAME_LEN>;

	protected:

		Vfs::Env &_env;

		Parent_fs &_parent_fs;

		Name const _name;

	private:

		Union_file_system _union { _env, *this, Ident { "union" } };

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

		struct Dir_channel : Vfs::Dir_channel
		{
			Dir_file_system &_fs;
			Allocator       &_alloc;

			Dir_channel(Dir_file_system &fs, Allocator &alloc)
			:
				_fs(fs), _alloc(alloc)
			{ }

			void destruct() override { destroy(_alloc, this); }

			Read_dir_result read_dir(At const at, Dirent_range_ptr const &dst) override
			{
				if (dst.num_entries < 1)
					return Read_dir_error::DENIED;

				if (at.pos > 0)
					return Read_dir_end();

				dst.start[0] = {
					.type = Dirent_type::DIRECTORY,
					.rwx  = Node_rwx::rwx(),
					.name = { _fs._name.string() }
				};
				return 1;
			}
		};

		friend class Vfs::Root;

		/**
		 * Constructor used by 'Root::Factory::Builtin_entry'
		 */
		Dir_file_system(Env &env, Parent_fs &parent_fs, Node const &node)
		:
			Dir_file_system(env, parent_fs,
			                node.attribute_value("name", Name()),
			                Ident::from_node(node))
		{ }

	protected:

		/*
		 * Parent_fs role for '_union' child of this dir file system
		 */

		void _with_compound_path(Span const &rel_path, auto const &fn) const
		{
			using Path = String<MAX_PATH_LEN>;
			Path { "/", _name, Cstring(rel_path.start, rel_path.num_bytes) }
				.with_span([&] (Span const &s) { fn(s); });
		}

		void notify_watchers(Span const &rel_path) override
		{
			_with_compound_path(rel_path, [&] (Span const &s) {
				_parent_fs.notify_watchers(s); });
		}

		void detach_channels(Span const &rel_path) override
		{
			_with_compound_path(rel_path, [&] (Span const &s) {
				_parent_fs.detach_channels(s); });
		}

	public:

		Dir_file_system(Env &env, Parent_fs &parent_fs, Name const &name,
		                Ident const &ident)
		:
			File_system(ident),
			_env(env), _parent_fs(parent_fs), _name(name)
		{ }

		Dir_file_system(Env &env, Parent_fs &parent_fs, Name const &name)
		:
			Dir_file_system(env, parent_fs, name, Ident { { "dir ", name } })
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
					.type              = Dirent_type::DIRECTORY,
					.rwx               = Node_rwx::rwx(),
					.modification_time = { },
				};
				return Stat_result::OK;
			}
			return _with_sub_path(path,
				[&] (auto const &path) { return _union.stat(path, out); },
				[&] () -> Stat_result  { return Stat_result::DENIED; });
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

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
		{
			if (_slash(path))
				return Open_error::DENIED; /* cannot open dir as file */

			return _with_sub_path(path,
				[&] (auto const &path) { return _union.open(path, attr, alloc); },
				[&] () -> Open_result  { return Open_error::DENIED; });
		}

		Opendir_result opendir(char const *path, Allocator &alloc) override
		{
			if (_slash(path)) {
				try {
					return *new (alloc) Dir_channel(*this, alloc);
				}
				catch (Out_of_ram)  { return Opendir_error::OUT_OF_RAM;  }
				catch (Out_of_caps) { return Opendir_error::OUT_OF_CAPS; }
			}

			return _with_sub_dir_path(path,
				[&] (char const *path)   { return _union.opendir(path, alloc); },
				[&] () -> Opendir_result { return Opendir_error::DENIED; });
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
				return Unlink_result::DENIED;

			return _with_sub_path(path,
				[&] (auto const &path)  { return _union.unlink(path); },
				[&] () -> Unlink_result { return Unlink_result::DENIED; });
		}

		Rename_result rename(char const *from_path, char const *to_path) override
		{
			/* deny renaming a path in the static VFS configuration */
			if (_slash(from_path))
				return Rename_result::DENIED;

			return _with_sub_path(from_path,
				[&] (auto const &from_path) {
					return _with_sub_path(to_path,
						[&] (auto const &to_path) {
							return _union.rename(from_path, to_path);
						},
						[&] () -> Rename_result {
							/* both paths must reside within the same file system */
							return Rename_result::DENIED;
						});
				},
				[&] () -> Rename_result { return Rename_result::DENIED; });
		}

		Mkdir_result mkdir(char const *path, Timestamp ts) override
		{
			if (_slash(path))
				return Mkdir_result::UPDATED;

			return _with_sub_path(path,
				[&] (auto const &path) { return _union.mkdir(path, ts); },
				[&] () -> Mkdir_result { return Mkdir_result::DENIED; });
		}

		Symlink_result symlink(char const *path, char const *target, Timestamp ts) override
		{
			if (_slash(path))
				return Symlink_result::DENIED;

			return _with_sub_path(path,
				[&] (auto const &path)   { return _union.symlink(path, target, ts); },
				[&] () -> Symlink_result { return Symlink_result::DENIED; });
		}

		Follow_result follow(char const *path, Byte_range_ptr const &dst) override
		{
			if (_slash(path))
				return Path_elem { 0 };

			return _with_sub_path(path,
				[&] (auto const &path) {
					return _union.follow(path, dst).template convert<Follow_result>(
						[&] (Path_elem elem) { return Path_elem { elem.index + 1 }; },
						[&] (Follow_error e) { return e; });
				},
				[&] () -> Follow_result { return Follow_error::DENIED; });
		}

		Progress update(Node const &node, Factory &factory) override
		{
			return _union.update(node, factory);
		}

		void resume_after_update() override { _union.resume_after_update(); }

		static constexpr auto BUILTIN_FS_TYPE = "dir";
};

#endif /* _INCLUDE__VFS__DIR_FILE_SYSTEM_H_ */
