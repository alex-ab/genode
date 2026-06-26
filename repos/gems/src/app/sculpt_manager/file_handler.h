 /*
 * \brief  Utility for watching 'Node' content on a file system
 * \author Norman Feske
 * \date   2026-04-02
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _FILE_HANDLER_H_
#define _FILE_HANDLER_H_

/* Genode includes */
#include <os/vfs.h>

namespace Genode {

	template <typename> class File_handler;
}


template <typename T>
struct Genode::File_handler
{
	private:

		using Path = Directory::Path;

		Allocator  &_alloc;
		Directory  &_dir;
		Path  const _path;

		Constructible<Readonly_file> _file { };

		size_t _file_size = 0;;

		using Allocation = Allocator::Allocation;

		Allocator::Alloc_result _buffer = Alloc_error::DENIED;

		T &_obj;
		void (T::*_member) (Node const &);

		Watch_handler<File_handler> _watch_handler { };

		void _handle_watch()
		{
			unsigned i = 0; /* retry if file size changed during read */
			unsigned const MAX_ATTEMPTS = 10;

			for (unsigned i = 0; i < MAX_ATTEMPTS; i++) {

				_file_size = 0;
				try { _file_size = size_t(_dir.file_size(_path)); }
				catch (...) { break; }

				if (!_file_size)
					break;

				size_t const buf_size = _buffer.convert<size_t>(
					[&] (Allocation const &a) { return a.num_bytes; },
					[&] (Alloc_error)         { return 0ul; });

				if (_file_size > buf_size)
					_buffer = _alloc.try_alloc(_file_size);

				if (!_file.constructed())
					try { _file.construct(_dir, _path); } catch (...) { }

				if (!_file.constructed()) {
					break;
				}

				bool ok = false;
				_buffer.with_result(
					[&] (Allocation &a) {

						if (_file->read({ (char *)a.ptr, _file_size }) != _file_size)
							return;

						(_obj.*_member)(Node { Span { (char *)a.ptr, _file_size } });
						ok = true;
					},
					[&] (Alloc_error) {
						error("alloc error in file handler for ", _path); });
				if (ok)
					break;
			}
			if (i == MAX_ATTEMPTS)
				error("giving up reading ", _path, " after ", MAX_ATTEMPTS, " attempts");
		}

	public:

		File_handler(Entrypoint &ep, Allocator &alloc, Directory &dir,
		             Path const &path, T &obj, void (T::*member)(Node const &))
		:
			_alloc(alloc), _dir(dir), _path(path), _obj(obj), _member(member),
			_watch_handler(ep, _dir, _path, *this, &File_handler::_handle_watch)
		{ }

		void with_node(auto const &fn) const
		{
			_buffer.with_result(
				[&] (Allocation const &a) {
					fn(Node { Span { (char *)a.ptr, _file_size } }); },
				[&] (Alloc_error) {
					fn(Node()); });
		}
};

#endif /* _FILE_HANDLER_H_ */
