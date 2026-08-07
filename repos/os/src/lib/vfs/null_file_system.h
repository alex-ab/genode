/*
 * \brief  null filesystem
 * \author Josef Soentgen
 * \author Norman Feske
 * \date   2012-07-31
 */

/*
 * Copyright (C) 2012-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__NULL_FILE_SYSTEM_H_
#define _INCLUDE__VFS__NULL_FILE_SYSTEM_H_

#include <vfs/single_file_system.h>

namespace Vfs_null {

	using namespace Genode;
	using namespace Genode::Vfs;

	struct File_system;
}


struct Vfs_null::File_system : Single_file_system
{
	File_system(Vfs::Env &, Parent_fs &parent_fs, Node const &config)
	:
		Single_file_system(parent_fs,
		                   Node_type::CONTINUOUS_FILE, name(),
		                   Node_rwx::rw(), config)
	{ }

	static char const *name()   { return "null"; }
	char const *type() override { return "null"; }

	struct Null_vfs_handle : Single_vfs_handle
	{
		Null_vfs_handle(Directory_service &ds, Allocator &alloc)
		:
			Single_vfs_handle(ds, alloc, 0)
		{ }

		Read_result read(At, Byte_range_ptr const &) override { return 0; }

		Write_result write(At, Const_byte_range_ptr const &src) override
		{
			return src.num_bytes;
		}

		bool read_ready()  const override { return false; }
		bool write_ready() const override { return true; }

		Ftruncate_result ftruncate(file_size) override { return FTRUNCATE_OK; }
	};

	Open_result open(char const  *path, unsigned,
	                 Vfs_handle **out_handle,
	                 Allocator   &alloc) override
	{
		if (!_single_file(path))
			return OPEN_ERR_UNACCESSIBLE;

		try {
			*out_handle = new (alloc) Null_vfs_handle(*this, alloc);
			return OPEN_OK;
		}
		catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
		catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
	}
};

#endif /* _INCLUDE__VFS__NULL_FILE_SYSTEM_H_ */
