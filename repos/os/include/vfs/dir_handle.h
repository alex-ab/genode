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

	private:

		Dir_handles &_handles;
		File_system &_root_dir;
		Allocator   &_alloc;

		Dir_handles::Element _elem { _handles, *this };

		struct { Dir_channel *_channel_ptr = nullptr; };

	public:

		Dir_handle(Dir_handles &handles, File_system &root_dir, Allocator &alloc,
		           Path path)
		:
			path(path), _handles(handles), _root_dir(root_dir), _alloc(alloc)
		{ }

		~Dir_handle() { detach(); }

		bool affected(Span const &path_prefix) const
		{
			/*
			 * Strip last path element from 'path_prefix' to detach the
			 * composite dir channel of a union fs whenever an immediate child
			 * is affected.
			 */
			return path.with_span([&] (Span const &s) {
				return s.starts_with(without_last_path_elem(path_prefix)); });
		}

		void detach()
		{
			if (_channel_ptr)
				_channel_ptr->destruct();

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
	Read_result result = Read_error::DENIED;

	if (!_channel_ptr)
		_root_dir.opendir(path.string(), _alloc).with_result(
			[&] (Dir_channel  &c) { _channel_ptr = &c; },
			[&] (Opendir_error e) { result = converted_error<Read_error>(e); });

	if (!_channel_ptr)
		return result;

	return _channel_ptr->read(at, dst).convert<Read_result>(
		[&] (size_t num_bytes) { return num_bytes; },
		[&] (Dir_channel::Read_error e) {
			switch (e) {
			case Dir_channel::Read_error::RETRY: return Read_error::RETRY;
			case Dir_channel::Read_error::DENIED: break;
			}
			return Read_error::DENIED;
		});
}

#endif /* _INCLUDE__VFS__DIR_HANDLE_H_ */
