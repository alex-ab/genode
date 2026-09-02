/*
 * \brief  File system that hosts a single node
 * \author Norman Feske
 * \date   2014-04-07
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__SINGLE_FILE_SYSTEM_H_
#define _INCLUDE__VFS__SINGLE_FILE_SYSTEM_H_

#include <vfs/file_system.h>
#include <vfs/vfs_handle.h>

namespace Genode::Vfs { class Single_file_system; }


class Genode::Vfs::Single_file_system : public File_system
{
	public:

		struct File /* used as namespace */
		{
			struct Name
			{
				String<64> string;

				static Name from_node(Node const &node)
				{
					decltype(string) const node_type { node.type() };
					return { node.attribute_value("name", node_type) };
				}
			};

			using Read  = Vfs::File::Read;
			using Write = Vfs::File::Write;

			struct Rwx { Read r; Write w; bool x; };

			static constexpr Rwx RO = {
				.r = Read::ANYWHERE,
				.w = Write::DENIED,
				.x = false };

			static constexpr Rwx RW_CONTINUOUS = {
				.r = Read::ANYWHERE,
				.w = Write::CONTINUOUS,
				.x = false };

			static constexpr Rwx WO_CONTINUOUS = {
				.r = Read::NOTHING,
				.w = Write::CONTINUOUS,
				.x = false };

			static constexpr Rwx RW_TRANSACTIONAL = {
				.r = Read::ANYWHERE,
				.w = Write::TRANSACTIONAL,
				.x = false };
		};

		File::Name const name;
		File::Rwx  const rwx;

	protected:

		Parent_fs &_parent_fs;

		bool _watched = false;

		Node_rwx _node_rwx() const
		{
			return { .readable   = rwx.r != File::Read::NOTHING,
			         .writeable  = rwx.w != File::Write::DENIED,
			         .executable = rwx.x };
		}

		struct Single_vfs_handle : Vfs_handle
		{
			using Vfs_handle::Vfs_handle;
		};

		struct Single_vfs_dir_handle : Vfs_handle, Noncopyable
		{
			Single_file_system &_fs;

			Single_vfs_dir_handle(Single_file_system &fs, Allocator &alloc)
			:
				Vfs_handle(fs, alloc, 0), _fs(fs)
			{ }

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (dst.num_bytes < sizeof(Dirent))
					return Read_error::DENIED;

				file_size const index = at.pos / sizeof(Dirent);
				if (index > 0)
					return Read_eof();

				Dirent &out = *(Dirent*)dst.start;
				out = {
					.type = (_fs.rwx.w == File::Write::TRANSACTIONAL)
					        ? Dirent_type::TRANSACTIONAL_FILE
					        : Dirent_type::CONTINUOUS_FILE,
					.rwx  = _fs._node_rwx(),
					.name = { _fs.name.string.string() }
				};
				return sizeof(Dirent);
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }
		};

		bool _root(const char *path)
		{
			return (strcmp(path, "") == 0) || (strcmp(path, "/") == 0);
		}

		bool _single_file(const char *path)
		{
			return (strlen(path) == (strlen(name.string.string()) + 1)) &&
			       (strcmp(&path[1], name.string.string()) == 0);
		}

		void _notify_watchers()
		{
			using Path = String<decltype(File::Name::string)::capacity() + 1>;
			Path { "/", name.string }.with_span([&] (Span const &s) {
				_parent_fs.notify_watchers({ s.start, s.num_bytes }); });
		}

	public:

		struct Attr
		{
			Ident      ident;
			File::Name name;
			File::Rwx  rwx;
		};

		Single_file_system(Parent_fs &parent_fs, Attr const &attr)
		:
			File_system(attr.ident), name(attr.name), rwx(attr.rwx),
			_parent_fs(parent_fs)
		{ }


		/*********************************
		 ** Directory-service interface **
		 *********************************/

		Stat_result stat(char const *path, Stat &out) override
		{
			out = Stat { };
			out.device = (addr_t)this;

			if (_root(path)) {
				out.type = Dirent_type::DIRECTORY;

			} else if (_single_file(path)) {
				out.type = (rwx.w == File::Write::TRANSACTIONAL)
				           ? Dirent_type::TRANSACTIONAL_FILE
				           : Dirent_type::CONTINUOUS_FILE,
				out.rwx  = _node_rwx();
			} else {
				return STAT_ERR_NO_ENTRY;
			}
			return STAT_OK;
		}

		unsigned num_dirent(char const *path) override
		{
			if (_root(path))
				return 1;
			else
				return 0;
		}

		bool directory(char const *path) override
		{
			if (_root(path))
				return true;

			return false;
		}

		bool dir_entry_exists(char const *path) override
		{
			return _single_file(path);
		}

		Opendir_result opendir(char const *path, Vfs_handle **out_handle,
		                       Allocator &alloc) override
		{
			if (!_root(path))
				return OPENDIR_ERR_LOOKUP_FAILED;

			try {
				*out_handle = new (alloc)
					Single_vfs_dir_handle(*this, alloc);
				return OPENDIR_OK;
			}
			catch (Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }
		}

		void close(Vfs_handle *handle) override
		{
			if (handle && (&handle->ds() == this))
				destroy(handle->alloc(), handle);
		}

		Unlink_result unlink(char const *path) override
		{
			if (_single_file(path))
				return UNLINK_ERR_NO_PERM;

			return UNLINK_ERR_NO_ENTRY;
		}

		Rename_result rename(char const *from, char const *to) override
		{
			if (_single_file(from) || _single_file(to))
				return RENAME_ERR_NO_PERM;
			return RENAME_ERR_NO_ENTRY;
		}

		Watch_result watch(char const *path) override
		{
			if (name.string == path)
				_watched = true;

			return Ok();
		}

		void unwatch(char const *path) override
		{
			if (name.string == path)
				_watched = false;
		}
};

#endif /* _INCLUDE__VFS__SINGLE_FILE_SYSTEM_H_ */
