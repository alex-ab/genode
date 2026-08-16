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
#include <vfs/file_system_factory.h>
#include <vfs/vfs_handle.h>

namespace Genode::Vfs { class Union_file_system; }


class Genode::Vfs::Union_file_system : public File_system, public Parent_fs
{
	private:

		/*
		 * Noncopyable
		 */
		Union_file_system(Union_file_system const &);
		Union_file_system &operator = (Union_file_system const &);

		Vfs::Env &_env;

		Parent_fs &_parent_fs;

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

				Read_result result = 0ul; /* EOF if no fs matches 'index' */

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

		/* pointer to first child file system */
		File_system *_first_file_system = nullptr;

		/* add new file system to the list of children */
		void _append_file_system(File_system *fs)
		{
			if (!_first_file_system) {
				_first_file_system = fs;
				return;
			}

			File_system *curr = _first_file_system;
			while (curr->next)
				curr = curr->next;

			curr->next = fs;
		}

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
			RES error = ok;

			/*
			 * The given path refers to at least one of our sub directories.
			 * Propagate the request into all of our file systems. If at least
			 * one operation succeeds, we return success.
			 */
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				RES const err = fn(*fs, path);

				if (err == ok)
					return err;

				if (err != no_entry && err != no_perm) {
					error = err;
				}

				if (err == no_perm)
					permission_denied = true;
			}

			/* none of our file systems could successfully operate on the path */
			return error != ok ? error : permission_denied ? no_perm : no_entry;
		}

		/*
		 * Accumulate number of directory entries that match in any of
		 * our sub file systems.
		 */
		unsigned _sum_dirents_of_file_systems(char const *path)
		{
			unsigned cnt = 0;
			for (File_system *fs = _first_file_system; fs; fs = fs->next)
				cnt += fs->num_dirent(path);
			return cnt;
		}

	protected:

		/**
		 * Parent_fs role for the children of this union file system
		 */
		void notify_watchers(Span const &rel_path) override
		{
			_parent_fs.notify_watchers(rel_path);
		}

	public:

		Union_file_system(Env &env, Parent_fs &parent_fs)
		:
			_env(env), _parent_fs(parent_fs)
		{ }

		Dataspace_capability dataspace(char const *path) override
		{
			if (!path)
				return Dataspace_capability();

			/*
			 * Query sub file systems for dataspace using the path local to
			 * the respective file system
			 */
			File_system *fs = _first_file_system;
			for (; fs; fs = fs->next) {
				Dataspace_capability ds = fs->dataspace(path);
				if (ds.valid())
					return ds;
			}

			return Dataspace_capability();
		}

		void release(char const *path, Dataspace_capability ds_cap) override
		{
			for (File_system *fs = _first_file_system; fs; fs = fs->next)
				fs->release(path, ds_cap);
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
					.type              = Node_type::DIRECTORY,
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
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {

				Stat_result const err = fs->stat(path, out);

				if (err == STAT_OK)
					return err;

				if (err != STAT_ERR_NO_ENTRY)
					return err;
			}

			/* none of our file systems felt responsible for the path */
			return STAT_ERR_NO_ENTRY;
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

			for (File_system *fs = _first_file_system; fs; fs = fs->next)
				if (fs->directory(path))
					return true;

			return false;
		}

		bool dir_entry_exists(char const *path) override
		{
			if (strlen(path) == 0)
				return true;

			for (File_system *fs = _first_file_system; fs; fs = fs->next)
				if (fs->dir_entry_exists(path))
					return true;

			return false;
		}

		Open_result open(char const  *path,
		                 unsigned     mode,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {
				Open_result const res = fs->open(path, mode, out_handle, alloc);
				if (res != OPEN_ERR_UNACCESSIBLE)
					return res;
			}

			/* path does not match any existing file or directory */
			return OPEN_ERR_UNACCESSIBLE;
		}

		/**
		 * Call 'opendir()' on each file system and store handles in
		 * a registry.
		 */
		Opendir_result _open_composite_dirs(Dir_vfs_handle &dir_vfs_handle)
		{
			Opendir_result res = OPENDIR_OK;
			try {
				for (File_system *fs = _first_file_system; fs; fs = fs->next) {
					Vfs_handle *child_handle = nullptr;
					Opendir_result const r =
						fs->opendir(dir_vfs_handle._path.string(), false,
						            &child_handle, dir_vfs_handle.alloc());
					switch (r) {
					case OPENDIR_OK:
						break;
					case OPENDIR_ERR_OUT_OF_RAM:
					case OPENDIR_ERR_OUT_OF_CAPS:
						return r;
					default:
						continue;
					}

					try {
						new (dir_vfs_handle.alloc())
							Dir_vfs_handle::Child_handle_element(
								dir_vfs_handle._child_handles, *fs, *child_handle);
					}
					catch (...) {
						child_handle->close();
						throw;
					}
					/* return OK because at least one directory has been opened */
					res = OPENDIR_OK;
				}
			}
			catch (Out_of_ram)  { res = OPENDIR_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { res = OPENDIR_ERR_OUT_OF_CAPS; }

			return res;
		}

		Opendir_result opendir(char const *path, bool create,
		                       Vfs_handle **out_handle, Allocator &alloc) override
		{
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

			for (File_system *fs = _first_file_system; fs; fs = fs->next) {
				result = fs->watch(path);
				if (result.failed())
					break;
			}

			if (result.failed())
				unwatch(path);

			return result;
		}

		void unwatch(char const *path) override
		{
			for (File_system *fs = _first_file_system; fs; fs = fs->next)
				fs->unwatch(path);
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
			Rename_result final = RENAME_ERR_NO_ENTRY;
			for (File_system *fs = _first_file_system; fs; fs = fs->next) {
				switch (fs->rename(from_path, to_path)) {
				case RENAME_OK:           return RENAME_OK;
				case RENAME_ERR_NO_ENTRY: continue;
				case RENAME_ERR_NO_PERM:  return RENAME_ERR_NO_PERM;
				case RENAME_ERR_CROSS_FS: final = RENAME_ERR_CROSS_FS;
				}
			}
			return final;
		}

		char const *type() override { return "dir"; }

		void update(Node const &node, File_system_factory &factory) override
		{
			using namespace Genode;

			/* construct child file systems only once */
			if (!_first_file_system) {
				node.for_each_sub_node([&] (Node const &sub_node) {

					File_system * const fs = factory.create(_env, *this, sub_node);
					if (fs) {
						fs->update(sub_node, factory);
						_append_file_system(fs);
						return;
					}

					error("failed to create VFS node: ", sub_node);
				});
			} else {

				/* propagate config parameter updates to child file systems */
				File_system *curr = _first_file_system;
				node.for_each_sub_node([&] (Node const &sub_node) {

					if (!curr) {
						error("VFS config update missed file system for ", sub_node);
						return;
					}

					/* check if type of node matches current file-system type */
					if (!curr || sub_node.has_type(curr->type()) == false) {
						error("VFS config update failed (node type '",
						      sub_node.type(), "' != fs type '", curr->type(),"')");
						return;
					}

					curr->update(sub_node, factory);
					curr = curr->next;
				});
			}
		}
};

#endif /* _INCLUDE__VFS__UNION_FILE_SYSTEM_H_ */
