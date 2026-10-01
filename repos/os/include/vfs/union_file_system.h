/*
 * \brief  Union file system
 * \author Norman Feske
 * \date   2026-08-16
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__UNION_FILE_SYSTEM_H_
#define _INCLUDE__VFS__UNION_FILE_SYSTEM_H_

#include <base/registry.h>
#include <vfs/env.h>

namespace Genode::Vfs { class Union_file_system; }


class Genode::Vfs::Union_file_system : public File_system, public Parent_fs
{
	private:

		Vfs::Env &_env;

		Parent_fs &_parent_fs;

		Constructible<Buffered_node> _config { };

		using Fs = Vfs::File_system;

		struct Child;

		using Children = List_model<Child>;

		struct Child : Children::Element
		{
			using Instance = Fs::Factory::Instance;

			Instance::Attempt _attempt;

			Child(Vfs::Env &env, Parent_fs &parent_fs, Factory &factory, Node const &node)
			:
				_attempt(factory.create(env, parent_fs, node))
			{
				if (_attempt.failed()) error("failed to create VFS node: ", node);
			}

			void with_fs(auto const &fn)
			{
				_attempt.with_result([&] (Instance &inst) { fn(inst.fs); },
				                     [&] (Factory::Error) { });
			}

			static bool type_matches(Node const &)
			{
				return true; /* capture all node types */
			}

			bool matches(Node const &node) const
			{
				return _attempt.convert<bool>(
					[&] (Instance const &inst) { return inst.fs.matches(node); },
					[&] (Factory::Error)       { return false; });
			}
		};

		Children _children { };

		void _for_each_fs(auto const &fn)
		{
			_children.for_each([&] (Child &c) { c.with_fs(fn); });
		}

		struct Dir_channel : Vfs::Dir_channel
		{
			struct Child_channel_element;

			using Child_channels = Registry<Child_channel_element>;

			struct Child_channel_element : Child_channels::Element
			{
				File_system &fs;
				Vfs::Dir_channel &channel;

				Child_channel_element(Child_channels &channels, File_system &fs,
				                      Vfs::Dir_channel &channel)
				:
					Child_channels::Element(channels, *this), fs(fs), channel(channel)
				{ }
			};

			Union_file_system &_fs;

			Allocator &_alloc;

			Absolute_path const _path;

			Child_channels _child_channels { };

			Read_result _read_of_file_systems(At const at, Byte_range_ptr const &dst)
			{
				size_t index = size_t(at.pos / sizeof(Dirent));

				/* base of composite directory index */
				size_t base = 0;

				bool done = false;

				Read_result result = Read_eof(); /* if no fs matches 'index' */

				_child_channels.for_each([&] (Child_channel_element const &e) {

					if (done) return; /* skip through */

					/*
					 * Determine number of matching directory entries within
					 * the current file system.
					 */
					unsigned const fs_num_dirent = e.fs.num_dirent(_path.string());

					/*
					 * Query directory entry if index lies with the file
					 * system.
					 */
					if (index - base < fs_num_dirent) {

						/* seek to file-system local index */
						index = index - base;

						result = e.channel.read(At { index*sizeof(Dirent) }, dst);
						done = true;
					}

					/* adjust base index for next file system */
					base += fs_num_dirent;
				});
				return result;
			}

			Dir_channel(Union_file_system &fs, Allocator &alloc, char const *path)
			:
				_fs(fs), _alloc(alloc), _path(path)
			{ }

			void destruct() override
			{
				_child_channels.for_each([&] (Child_channel_element &e) {
					e.channel.destruct();
					destroy(_alloc, &e);
				});

				destroy(_alloc, this);
			}

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (dst.num_bytes < sizeof(Dirent))
					return Read_error::DENIED;

				return _read_of_file_systems(at, dst);
			}
		};

		/**
		 * Returns if path corresponds to top directory of file system
		 */
		bool _top_dir(char const *path) const { return strcmp(path, "/") == 0; }

		/*
		 * Accumulate number of directory entries that match in any of
		 * our sub file systems.
		 */
		unsigned _sum_dirents_of_file_systems(char const *path)
		{
			unsigned cnt = 0;
			_for_each_fs([&] (Fs &fs) { cnt += fs.num_dirent(path); });
			return cnt;
		}

		bool _update_in_progress = false; /* safeguard for error diagnostics */

	protected:

		/**
		 * Parent_fs role for the children of this union file system
		 */
		void notify_watchers(Span const &rel_path) override
		{
			_parent_fs.notify_watchers(rel_path);
		}

		void detach_channels(Span const &rel_path) override
		{
			_parent_fs.detach_channels(rel_path);
		}

	public:

		Union_file_system(Env &env, Parent_fs &parent_fs, Ident const &ident)
		:
			File_system(ident), _env(env), _parent_fs(parent_fs)
		{ }

		Dataspace_capability dataspace(char const *path) override
		{
			Dataspace_capability result { };
			_for_each_fs([&] (Fs &fs) {
				if (!result.valid())
					result = fs.dataspace(path); });

			return result;
		}

		void release(char const *path, Dataspace_capability ds_cap) override
		{
			_for_each_fs([&] (Fs &fs) { fs.release(path, ds_cap); });
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			/*
			 * If path equals directory name, return information about the
			 * current directory.
			 */
			if (strlen(path) == 0 || _top_dir(path)) {
				out = {
					.size              = 0,
					.type              = Dirent_type::DIRECTORY,
					.rwx               = Node_rwx::rwx(),
					.modification_time = { },
				};
				return Stat_result::OK;
			}

			/*
			 * The given path refers to one of our sub directories.
			 * Propagate the request into our file systems.
			 */
			Stat_result result = Stat_result::DENIED;
			_for_each_fs([&] (Fs &fs) {
				if (result == Stat_result::DENIED)
					result = fs.stat(path, out); });

			return result;
		}

		unsigned num_dirent(char const *path) override
		{
			return _sum_dirents_of_file_systems(path);
		}

		/**
		 * Return true if specified path is a directory
		 */
		bool directory(char const *path) override
		{
			if (_top_dir(path))
				return true;

			if (strlen(path) == 0)
				return true;

			bool exists = false;
			_for_each_fs([&] (Fs &fs) {
				if (!exists) exists = fs.directory(path); });

			return exists;
		}

		bool dir_entry_exists(char const *path) override
		{
			if (strlen(path) == 0)
				return true;

			bool exists = false;
			_for_each_fs([&] (Fs &fs) {
				if (!exists) exists = fs.dir_entry_exists(path); });

			return exists;
		}

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
		{
			if (_update_in_progress)
				error("attempt to access file '", path, "' during VFS update");

			Open_error error = Open_error::DENIED;
			File_channel *channel_ptr = nullptr;
			_for_each_fs([&] (Fs &fs) {
				if (!channel_ptr && error == Open_error::DENIED)
					fs.open(path, attr, alloc).with_result(
						[&] (File_channel &c) { channel_ptr = &c; },
						[&] (Open_error e)    { error = e; });
			});
			if (channel_ptr)
				return *channel_ptr;

			return error;
		}

		using Open_composite_dirs_result = Attempt<Ok, Opendir_error>;

		/**
		 * Call 'opendir()' on each file system and store channels in
		 * a registry.
		 */
		Open_composite_dirs_result _open_composite_dirs(Dir_channel &dir_channel)
		{
			auto quota_exceeded = [] (Opendir_result r)
			{
				return r == Opendir_error::OUT_OF_RAM || r == Opendir_error::OUT_OF_CAPS;
			};

			auto const &path = dir_channel._path.string();

			Opendir_error error = Opendir_error::DENIED;
			bool at_least_one_ok = false;

			_for_each_fs([&] (Fs &fs) {
				if (quota_exceeded(error))
					return;

				fs.opendir(path, dir_channel._alloc).with_result(
					[&] (Vfs::Dir_channel &c) {
						try {
							new (dir_channel._alloc)
								Dir_channel::Child_channel_element(
									dir_channel._child_channels, fs, c);
							at_least_one_ok = true;
						}
						catch (Out_of_ram)  { error = Opendir_error::OUT_OF_RAM;  }
						catch (Out_of_caps) { error = Opendir_error::OUT_OF_CAPS; }

						if (quota_exceeded(error))
							c.destruct();
					},
					[&] (Opendir_error e) { error = e; });
			});

			if (at_least_one_ok && !quota_exceeded(error))
				return Ok();

			return error;
		}

		Opendir_result opendir(char const *path, Allocator &alloc) override
		{
			if (_update_in_progress)
				error("attempt to access dir '", path, "' during VFS update");

			Opendir_result result = Opendir_error::DENIED;

			try {
				Dir_channel &channel = *new (alloc) Dir_channel(*this, alloc, path);

				return _open_composite_dirs(channel).convert<Opendir_result>(
					[&] (Ok) -> Dir_channel & { return channel; },
					[&] (Opendir_error e) {
						channel.destruct();
						return e; });
			}
			catch (Out_of_ram)  { return Opendir_error::OUT_OF_RAM;  }
			catch (Out_of_caps) { return Opendir_error::OUT_OF_CAPS; }
		}

		Watch_result watch(char const *path) override
		{
			Watch_result result = Ok();

			_for_each_fs([&] (Fs &fs) {
				if (result.ok())
					result = fs.watch(path); });

			if (result.failed())
				unwatch(path);

			return result;
		}

		void unwatch(char const *path) override
		{
			_for_each_fs([&] (Fs &fs) { fs.unwatch(path); });
		}

		Unlink_result unlink(char const *path) override
		{
			if (strlen(path) == 0)
				return Unlink_result::DENIED;

			/*
			 * The given path refers to at least one of our sub directories.
			 * Propagate the request into all of our file systems. If at least
			 * one operation succeeds, we return success.
			 */
			Unlink_result result = Unlink_result::DENIED;
			_for_each_fs([&] (Fs &fs) {
				if (result == Unlink_result::DENIED)
					result = fs.unlink(path); });

			return result;
		}

		Rename_result rename(char const *from_path, char const *to_path) override
		{
			Rename_result result = Rename_result::DENIED;
			_for_each_fs([&] (Fs &fs) {
				if (result == Rename_result::DENIED)
					result = fs.rename(from_path, to_path); });

			return result;
		}

		Mkdir_result mkdir(char const *path, Timestamp ts) override
		{
			if (_top_dir(path))
				return Mkdir_result::UPDATED;

			Mkdir_result result = Mkdir_result::DENIED;
			_for_each_fs([&] (Fs &fs) {
				if (result == Mkdir_result::DENIED)
					result = fs.mkdir(path, ts); });
			return result;
		}

		Symlink_result symlink(char const *path, char const *target, Timestamp ts) override
		{
			if (_top_dir(path))
				return Symlink_result::DENIED;

			Symlink_result result = Symlink_result::DENIED;
			_for_each_fs([&] (Fs &fs) {
				if (result == Symlink_result::DENIED)
					result = fs.symlink(path, target, ts); });
			return result;
		}

		Follow_result follow(char const *path, Byte_range_ptr const &dst) override
		{
			if (_top_dir(path))
				return Path_elem { 0 };

			Follow_result result = Follow_error::DENIED;
			_for_each_fs([&] (Fs &fs) {
				if (result == Follow_error::DENIED || result == Follow_error::NO_SYMLINK)
					result = fs.follow(path, dst); });
			return result;
		}

		Progress update(Node const &node, Factory &factory) override
		{
			if (_config.constructed() && !_config->differs_from(node))
				return STALLED;

			_config.construct(_env.alloc(), node);

			using namespace Genode;

			Progress result = STALLED;
			_update_in_progress = true;

			_children.update_from_node(node,

				[&] (Node const &node) -> Child & {
					result = PROGRESSED;
					return *new (_env.alloc()) Child(_env, *this, factory, node);
				},
				[&] (Child &child) {
					child.with_fs([&] (Fs &fs) {
						(void)fs.update(Node(), factory); });
					destroy(_env.alloc(), &child);
					result = PROGRESSED;
				},
				[&] (Child &child, Node const &node) {
					child.with_fs([&] (Fs &fs) {
						if (fs.update(node, factory).progressed)
							result = PROGRESSED; });
				}
			);
			_update_in_progress = false;
			return result;
		}

		void resume_after_update() override
		{
			_for_each_fs([&] (Fs &fs) { fs.resume_after_update(); });
		}
};

#endif /* _INCLUDE__VFS__UNION_FILE_SYSTEM_H_ */
