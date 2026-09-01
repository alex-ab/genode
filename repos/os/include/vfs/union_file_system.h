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
#include <vfs/vfs_handle.h>
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

		struct Dir_vfs_handle : Vfs_handle
		{
			struct Child_handle_element;

			using Child_handles = Registry<Child_handle_element>;

			struct Child_handle_element : Child_handles::Element
			{
				File_system &fs;
				Vfs_handle  &handle;
				Child_handle_element(Child_handles &handles, File_system &fs,
				                     Vfs_handle &handle)
				:
					Child_handles::Element(handles, *this), fs(fs), handle(handle)
				{ }
			};

			Union_file_system &_fs;

			Absolute_path const _path;

			Child_handles _child_handles { };

			Read_result _read_of_file_systems(At const at, Byte_range_ptr const &dst)
			{
				size_t index = size_t(at.pos / sizeof(Dirent));

				/* base of composite directory index */
				size_t base = 0;

				bool done = false;

				Read_result result = Read_eof(); /* if no fs matches 'index' */

				_child_handles.for_each([&] (Child_handle_element const &e) {

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

						result = e.handle.read(At { index*sizeof(Dirent) }, dst);
						done = true;
					}

					/* adjust base index for next file system */
					base += fs_num_dirent;
				});
				return result;
			}

			Dir_vfs_handle(Union_file_system &fs, Allocator &alloc, char const *path)
			:
				Vfs_handle(fs, alloc, 0), _fs(fs), _path(path)
			{ }

			~Dir_vfs_handle()
			{
				_child_handles.for_each([&] (Child_handle_element &e) {
					e.handle.close();
					destroy(alloc(), &e);
				});
			}

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (dst.num_bytes < sizeof(Dirent))
					return Read_error::DENIED;

				return _read_of_file_systems(at, dst);
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return false; }
		};

		/**
		 * Returns if path corresponds to top directory of file system
		 */
		bool _top_dir(char const *path) const { return strcmp(path, "/") == 0; }

		/**
		 * Perform operation on a file system
		 *
		 * \param fn  functor that takes a file-system reference and
		 *            the path as arguments
		 */
		template <typename RES>
		RES _dir_op(RES const no_entry, RES const no_perm, RES const ok,
		            char const *path, auto const &fn)
		{
			/*
			 * Prevent operation if path equals directory name defined
			 * via the static VFS configuration.
			 */
			if (strlen(path) == 0)
				return no_perm;

			/*
			 * If any of the sub file systems returns a permission error and
			 * there exists no sub file system that takes the request, we
			 * return the permission error.
			 */
			bool permission_denied = false;

			/*
			 * Keep the most meaningful error code. When using stacked file
			 * systems, most child file systems will eventually return no
			 * entry (or leave the error code unchanged). If any of those
			 * file systems has anything more interesting to tell, return
			 * this information after all file systems have been tried and
			 * none could handle the request.
			 */
			RES result = no_entry;

			/*
			 * The given path refers to at least one of our sub directories.
			 * Propagate the request into all of our file systems. If at least
			 * one operation succeeds, we return success.
			 */
			bool done = false;
			_for_each_fs([&] (Fs &fs) {
				if (done) return;

				RES const err = fn(fs, path);

				if (err == ok) { done = true; result = ok; }

				if (err != no_entry && err != no_perm) result = err;

				if (err == no_perm) { permission_denied = true; };
			});

			if (done && result == ok) return ok;
			if (permission_denied)    return no_perm;
			return no_entry;
		}

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
					.device            = (addr_t)this,
					.modification_time = { },
				};
				return STAT_OK;
			}

			/*
			 * The given path refers to one of our sub directories.
			 * Propagate the request into our file systems.
			 */
			Stat_result result = STAT_ERR_NO_ENTRY;
			_for_each_fs([&] (Fs &fs) {
				if (result == STAT_ERR_NO_ENTRY)
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

		Open_result open(char const  *path,
		                 unsigned     mode,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (_update_in_progress)
				error("attempt to access file '", path, "' during VFS update");

			Open_result result = OPEN_ERR_UNACCESSIBLE;

			_for_each_fs([&] (Fs &fs) {
				if (result == OPEN_ERR_UNACCESSIBLE)
					result = fs.open(path, mode, out_handle, alloc); });

			return result;
		}

		/**
		 * Call 'opendir()' on each file system and store handles in
		 * a registry.
		 */
		Opendir_result _open_composite_dirs(Dir_vfs_handle &dir_vfs_handle)
		{
			auto quota_exceeded = [] (Opendir_result r)
			{
				return r == OPENDIR_ERR_OUT_OF_RAM || r == OPENDIR_ERR_OUT_OF_CAPS;
			};

			Opendir_result result = OPENDIR_OK;
			bool at_least_one_ok = false;

			_for_each_fs([&] (Fs &fs) {
				if (quota_exceeded(result))
					return;

				Vfs_handle *child_handle_ptr = nullptr;
				result = fs.opendir(dir_vfs_handle._path.string(), false,
				                    &child_handle_ptr, dir_vfs_handle.alloc());
				if (quota_exceeded(result))
					return;

				if (result == OPENDIR_OK && child_handle_ptr) {
					at_least_one_ok = true;
					try {
						new (dir_vfs_handle.alloc())
							Dir_vfs_handle::Child_handle_element(
								dir_vfs_handle._child_handles, fs, *child_handle_ptr);
					}
					catch (Out_of_ram)  { result = OPENDIR_ERR_OUT_OF_RAM; }
					catch (Out_of_caps) { result = OPENDIR_ERR_OUT_OF_CAPS; }

					if (quota_exceeded(result))
						child_handle_ptr->close();
				}
			});
			return at_least_one_ok ? OPENDIR_OK : result;
		}

		Opendir_result opendir(char const *path, bool create,
		                       Vfs_handle **out_handle, Allocator &alloc) override
		{
			if (_update_in_progress)
				error("attempt to access dir '", path, "' during VFS update");

			Opendir_result result = OPENDIR_OK;

			if (_top_dir(path)) {
				if (create)
					return OPENDIR_ERR_PERMISSION_DENIED;

				/*
				 * opendir with '/' (called from 'open_composite_dirs' returns handle
				 * only, VFS root additionally calls 'open_composite_dirs' in order to
				 * open its file systems
				 */
				Dir_vfs_handle *root_handle;
				try {
					root_handle = new (alloc) Dir_vfs_handle(*this, alloc, path);
				}
				catch (Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM; }
				catch (Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }

				result = _open_composite_dirs(*root_handle);
				if (result == OPENDIR_OK)
					*out_handle = root_handle;
				else
					close(root_handle);

				return result;
			}

			if (create) {
				if (dir_entry_exists(path))
					return OPENDIR_ERR_NODE_ALREADY_EXISTS;

				auto opendir_fn = [&] (File_system &fs, char const *path)
				{
					Vfs_handle *tmp_handle;
					Opendir_result opendir_result =
						fs.opendir(path, true, &tmp_handle, alloc);

					if (opendir_result == OPENDIR_OK)
						tmp_handle->close();

					return opendir_result; /* return from lambda */
				};

				Opendir_result opendir_result =
					_dir_op(OPENDIR_ERR_LOOKUP_FAILED,
					        OPENDIR_ERR_PERMISSION_DENIED,
					        OPENDIR_OK,
					        path, opendir_fn);

				if (opendir_result != OPENDIR_OK)
					return opendir_result;
			}

			Dir_vfs_handle *dir_vfs_handle;
			try {
				dir_vfs_handle = new (alloc) Dir_vfs_handle(*this, alloc, path);
			}
			catch (Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }

			result = _open_composite_dirs(*dir_vfs_handle);
			if (result == OPENDIR_OK) {
				*out_handle = dir_vfs_handle;
			} else {
				/* close the master handle and the rest will follow */
				close(dir_vfs_handle);
			}
			return result;
		}

		Openlink_result openlink(char const *path, bool create,
		                         Vfs_handle **out_handle,
		                         Allocator &alloc) override
		{
			auto openlink_fn = [&] (File_system &fs, char const *path)
			{
				return fs.openlink(path, create, out_handle, alloc);
			};

			return _dir_op(OPENLINK_ERR_LOOKUP_FAILED,
			               OPENLINK_ERR_PERMISSION_DENIED,
			               OPENLINK_OK,
			               path, openlink_fn);
		}

		void close(Vfs_handle *handle) override
		{
			if (handle && (&handle->ds() == this))
				destroy(handle->alloc(), handle);
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
			auto unlink_fn = [] (File_system &fs, char const *path)
			{
				return fs.unlink(path);
			};

			return _dir_op(UNLINK_ERR_NO_ENTRY, UNLINK_ERR_NO_PERM, UNLINK_OK,
			               path, unlink_fn);
		}

		Rename_result rename(char const *from_path, char const *to_path) override
		{
			Rename_result result = RENAME_ERR_NO_ENTRY;
			_for_each_fs([&] (Fs &fs) {
				if (result == RENAME_ERR_NO_ENTRY)
					result = fs.rename(from_path, to_path); });

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
