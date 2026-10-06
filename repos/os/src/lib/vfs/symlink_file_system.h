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

		struct Symlink_dir_channel : Vfs::Dir_channel
		{
			File_system &_fs;
			Allocator   &_alloc;

			Symlink_dir_channel(File_system &fs, Allocator &alloc)
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
					.type = Dirent_type::SYMLINK,
					.rwx  = Node_rwx::ro(),
					.name = { _fs.Single_file_system::name.string.string() }
				};
				return 1;
			}
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

		Opendir_result opendir(char const *path, Allocator &alloc) override
		{
			if (!_root(path))
				return Opendir_error::DENIED;

			try {
				return *new (alloc) Symlink_dir_channel(*this, alloc);
			}
			catch (Out_of_ram)  { return Opendir_error::OUT_OF_RAM;  }
			catch (Out_of_caps) { return Opendir_error::OUT_OF_CAPS; }
		}

		Follow_result follow(char const *path, Byte_range_ptr const &dst) override
		{
			if (!_single_file(path))
				return Follow_error::DENIED;

			_target.with_span([&] (Span const &src) {
				size_t n = min(dst.num_bytes,
				               src.num_bytes + 1 /* null termination */);
				copy_cstring(dst.start, src.start, n); });

			return Path_elem { 0 };
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			out = Stat { };

			if (_single_file(path)) {
				out.type = Dirent_type::SYMLINK,
				out.rwx  = Node_rwx::ro();
				return Stat_result::OK;
			}
			return Stat_result::DENIED;
		}

		static constexpr auto BUILTIN_FS_TYPE = "symlink";
};

#endif /* _INCLUDE__VFS__SYMLINK_FILE_SYSTEM_H_ */
