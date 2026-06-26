/*
 * \brief  Watch mechanism for monitoring files or directories for changes
 * \author Norman Feske
 * \date   2026-06-10
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__WATCH_HANDLE_H_
#define _INCLUDE__VFS__WATCH_HANDLE_H_

#include <base/registry.h>
#include <vfs/file_system.h>

namespace Genode::Vfs {
	class Watch_handles;
	class Watch_handle;
}


struct Genode::Vfs::Watch_handles : Registry<Watch_handle> { };


class Genode::Vfs::Watch_handle : Noncopyable
{
	public:

		using Path = String<MAX_PATH_LEN>;

		struct Handler : Interface
		{
			/**
			 * Called whenever a watched file/directory changes
			 *
			 * Note that 'io_handle_watch' is executed at I/O signal level.
			 * The implementation should not call into application-level code.
			 */
			virtual void io_handle_watch() = 0;
		};

	private:

		Watch_handles &_handles;
		File_system   &_root_dir;

		Watch_handles::Element _elem { _handles, *this };

		bool _watching = false;

		unsigned _num_watchers(Path const &path) const
		{
			unsigned count = 0;
			_handles.for_each([&] (Watch_handle const &handle) {
				if (handle.path == path) count++; });
			return count;
		}

	public:

		Path const path;

		Handler &handler;

		Watch_handle(Watch_handles &handles, File_system &root_dir,
		             Path const &path, Handler &handler)
		:
			_handles(handles), _root_dir(root_dir), path(path), handler(handler)
		{ }

		~Watch_handle() { unwatch(); }

		Watch_result watch()
		{
			Watch_result result = Ok();

			/* watch path if this handle is the first one watching */
			if (!_watching && _num_watchers(path) == 1)
				result = _root_dir.watch(path.string());

			if (result.failed())
				return result;

			_watching = true;

			return Ok();
		}

		void unwatch()
		{
			/* unwatch path if this handle is the last one watching */
			if (_watching && _num_watchers(path) <= 1)
				_root_dir.unwatch(path.string());

			_watching = false;
		}
};

#endif /* _INCLUDE__VFS__WATCH_HANDLE_H_ */
