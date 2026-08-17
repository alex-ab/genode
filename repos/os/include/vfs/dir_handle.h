/*
 * \brief  Interface for accessing a directory
 * \author Norman Feske
 * \date   2026-08-15
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__DIR_HANDLE_H_
#define _INCLUDE__VFS__DIR_HANDLE_H_

#include <base/registry.h>
#include <vfs/file_system.h>
#include <vfs/vfs_handle.h>

namespace Genode::Vfs {
	class Dir_handles;
	class Dir_handle;
}


struct Genode::Vfs::Dir_handles : Registry<Dir_handle> { };


class Genode::Vfs::Dir_handle : Noncopyable
{
	public:

		using Path = String<MAX_PATH_LEN>;

		Path const path;

		using Channel = Vfs_handle;

	private:

		Dir_handles &_handles;
		File_system &_root_dir;
		Allocator   &_alloc;

		Dir_handles::Element _elem { _handles, *this };

		struct { Channel *_channel_ptr = nullptr; };

	public:

		Dir_handle(Dir_handles &handles, File_system &root_dir, Allocator &alloc,
		           Path path)
		:
			path(path), _handles(handles), _root_dir(root_dir), _alloc(alloc)
		{ }

		~Dir_handle() { detach(); }

		void detach()
		{
			if (_channel_ptr)
				_channel_ptr->ds().close(_channel_ptr);

			_channel_ptr = nullptr;
		}

		/**
		 * Initiate or complete read of directory entries
		 *
		 * On success, the method returns the number of read bytes.
		 * If zero, the end of the directory is reached.
		 *
		 * \return Read_error::RETRY  if the read operation is not yet
		 *                            complete and must by tried again once
		 *                            external I/O has progressed
		 */
		inline Read_result read(At at, Byte_range_ptr const &dst);
};



Genode::Vfs::Read_result
Genode::Vfs::Dir_handle::read(At at, Byte_range_ptr const &dst)
{
	if (!_channel_ptr) {
		Directory_service::Opendir_result const result =
			_root_dir.opendir(path.string(), { }, &_channel_ptr, _alloc);

		switch (result) {
		case Directory_service::OPENDIR_ERR_PERMISSION_DENIED:
		case Directory_service::OPENDIR_ERR_LOOKUP_FAILED:
		case Directory_service::OPENDIR_ERR_NODE_ALREADY_EXISTS:
		case Directory_service::OPENDIR_ERR_NAME_TOO_LONG:
		case Directory_service::OPENDIR_ERR_NO_SPACE:    return 0; /* EOF */
		case Directory_service::OPENDIR_ERR_OUT_OF_RAM:  return Read_error::OUT_OF_RAM;
		case Directory_service::OPENDIR_ERR_OUT_OF_CAPS: return Read_error::OUT_OF_CAPS;
		case Directory_service::OPENDIR_OK: break;
		}
	}
	if (!_channel_ptr)
		return 0; /* EOF */

	return _channel_ptr->read(at, dst).convert<Read_result>(
		[&] (size_t num_bytes) { return num_bytes; },
		[&] (Channel::Read_error e) {
			switch (e) {
			case Channel::Read_error::RETRY: return Read_error::RETRY;
			case Channel::Read_error::DENIED: break;
			}
			return Read_error::DENIED;
		});
}

#endif /* _INCLUDE__VFS__DIR_HANDLE_H_ */
