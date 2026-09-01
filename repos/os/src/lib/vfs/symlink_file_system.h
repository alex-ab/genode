/*
 * \brief  Symlink filesystem
 * \author Norman Feske
 * \date   2015-08-21
 *
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__SYMLINK_FILE_SYSTEM_H_
#define _INCLUDE__VFS__SYMLINK_FILE_SYSTEM_H_

#include <vfs/single_file_system.h>

namespace Vfs_symlink {

	using namespace Genode;
	using namespace Genode::Vfs;

	class File_system;
}


class Vfs_symlink::File_system : public Single_file_system
{
	private:

		using Target = String<MAX_PATH_LEN>;

		Target const _target;

		struct Symlink_handle final : Single_vfs_handle
		{
			File_system const &_fs;

			Symlink_handle(File_system &fs, Allocator &alloc)
			:
				Single_vfs_handle(fs, alloc, 0), _fs(fs)
			{ }

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (at.pos != 0)
					return Read_error::DENIED;

				size_t const n = min(dst.num_bytes, _fs._target.length());
				copy_cstring(dst.start, _fs._target.string(), n);
				return (n > 0) ? n - 1 : 0;
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return false; }
		};

		struct Symlink_dir_handle : Vfs_handle, Noncopyable
		{
			File_system &_fs;

			Symlink_dir_handle(File_system &fs, Allocator &alloc)
			:
				Vfs_handle(fs, alloc, 0), _fs(fs)
			{ }

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (dst.num_bytes < sizeof(Dirent))
					return Read_error::DENIED;

				file_size index = at.pos / sizeof(Dirent);

				Dirent &out = *(Dirent*)dst.start;

				if (index == 0) {
					out = {
						.type = Dirent_type::SYMLINK,
						.rwx  = Node_rwx::ro(),
						.name = { _fs.Single_file_system::name.string.string() }
					};
				} else {
					out = {
						.type = Dirent_type::END,
						.rwx  = { },
						.name = { }
					};
				}

				return sizeof(Dirent);
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }
		};

	public:

		File_system(Vfs::Env &, Parent_fs &parent_fs, Node const &config)
		:
			Single_file_system(parent_fs, {
				.ident = Ident::from_node(config),
				.name  = File::Name::from_node(config),
				.rwx   = File::RO
			}),
			_target(config.attribute_value("target", Target()))
		{ }

		Opendir_result opendir(char const *path, bool create,
		                       Vfs_handle **out_handle,
		                       Allocator &alloc) override
		{
			if (!_root(path))
				return OPENDIR_ERR_LOOKUP_FAILED;

			if (create)
				return OPENDIR_ERR_PERMISSION_DENIED;

			try {
				*out_handle = new (alloc) Symlink_dir_handle(*this, alloc);
				return OPENDIR_OK;
			}
			catch (Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }
		}

		Open_result open(char const *, unsigned, Vfs_handle **, Allocator&) override {
			return OPEN_ERR_UNACCESSIBLE; }

		Openlink_result openlink(char const *path, bool create,
		                         Vfs_handle **out_handle, Allocator &alloc) override
		{
			if (!_single_file(path))
				return OPENLINK_ERR_LOOKUP_FAILED;

			if (create)
				return OPENLINK_ERR_NODE_ALREADY_EXISTS;

			try {
				*out_handle = new (alloc) Symlink_handle(*this, alloc);
				return OPENLINK_OK;
			}
			catch (Out_of_ram)  { return OPENLINK_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPENLINK_ERR_OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			out = Stat { };
			out.device = (addr_t)this;

			if (_single_file(path)) {
				out.type = Node_type::SYMLINK,
				out.rwx  = Node_rwx::ro();
			} else {
				return STAT_ERR_NO_ENTRY;
			}
			return STAT_OK;
		}

		static constexpr auto BUILTIN_FS_TYPE = "symlink";
};

#endif /* _INCLUDE__VFS__SYMLINK_FILE_SYSTEM_H_ */
