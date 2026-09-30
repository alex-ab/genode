/*
 * \brief  Interface for accessing a file
 * \author Norman Feske
 * \date   2026-08-12
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__FILE_HANDLE_H_
#define _INCLUDE__VFS__FILE_HANDLE_H_

#include <base/registry.h>
#include <vfs/file_system.h>

namespace Genode::Vfs {
	class File_handles;
	class File_handle;
}


struct Genode::Vfs::File_handles : Registry<File_handle> { };


class Genode::Vfs::File_handle : Noncopyable
{
	public:

		using Path = String<MAX_PATH_LEN>;

		Path const path;
		bool const writeable;

		using Channel = File_channel;

		enum class Attach_error { RETRY, DENIED, OUT_OF_RAM, OUT_OF_CAPS };

		using Attach_result = Attempt<Ok, Attach_error>;

	private:

		File_handles &_handles;
		File_system  &_root_dir;
		Allocator    &_alloc;

		File_handles::Element _elem { _handles, *this };

		bool _need_sync = writeable; /* might be a new file */

		bool _read_ready_requested = false;

		struct { Channel *_channel_ptr = nullptr; };

		template <typename ERR>
		static ERR _converted(Attach_error e) { return converted_error<ERR>(e); }

		template <typename ERR_FN>
		auto _with_channel(auto const &fn, ERR_FN const &err_fn)
		-> typename Trait::Functor<decltype(&ERR_FN::operator())>::Return_type
		{
			bool const orig_detached = _channel_ptr == nullptr;

			if (!_channel_ptr) {
				Open_error error = Open_error::DENIED;

				auto try_open_existing_file = [&]
				{
					_root_dir.open(path.string(), { .writeable = writeable, .create = false },
					               _alloc).with_result(
						[&] (File_channel &c) { _channel_ptr = &c; },
						[&] (Open_error e)    { error = e; });
				};

				if (!writeable)
					try_open_existing_file();
				else
					_root_dir.open(path.string(), { .writeable = writeable, .create = true },
					               _alloc).with_result(
						[&] (File_channel &c) { _channel_ptr = &c; },
						[&] (Open_error e) {
							error = e;
							if (e != Open_error::DENIED)
								return;

							/* reattempt w/o create flag */
							try_open_existing_file();
						});

				if (!_channel_ptr)
					return err_fn(converted_error<Attach_error>(error));
			}

			if (_channel_ptr) {

				if (response_handler_ptr)
					_channel_ptr->handler(*response_handler_ptr);

				if (orig_detached && _read_ready_requested) {

					/* re-subscribe after detach */
					(void)_channel_ptr->read_ready();

					/* trigger artificial read-ready wakeup */
					response_handler_ptr->read_ready_response();
				}

				return fn(*_channel_ptr);
			}

			error("VFS file channel opened but inaccessible");
			return err_fn(Attach_error::DENIED);
		}

	public:

		struct Attr { Path path; bool writeable; };

		File_handle(File_handles &handles, File_system &root_dir, Allocator &alloc,
		            Attr const attr)
		:
			path(attr.path), writeable(attr.writeable),
			_handles(handles), _root_dir(root_dir), _alloc(alloc)
		{ }

		~File_handle()
		{
			if (_channel_ptr && detach() == Detach_result::RETRY)
				warning("destructing still attached VFS file handle for ", path);
		}

		bool affected(Span const &path_prefix) const
		{
			return path.with_span([&] (Span const &s) { return s.starts_with(path_prefix); });
		}

		Attach_result attach()
		{
			return _with_channel(
				[&] (Channel &) { return Ok(); },
				[&] (Attach_error e) -> Attach_result { return _converted<Attach_error>(e); });
		}

		enum class Detach_result { OK, RETRY };

		Detach_result detach()
		{
			if (!_channel_ptr)
				return Detach_result::OK;

			if (_need_sync) {
				if (sync() == Sync_result::RETRY)
					return Detach_result::RETRY;
				_need_sync = false;
			}

			_channel_ptr->destruct();
			_channel_ptr = nullptr;
			return Detach_result::OK;
		}

		/**
		 * Initiate or complete read operation
		 *
		 * On success, the method returns the number of read bytes.
		 * If zero, the end of file is reached.
		 *
		 * \return Read_error::RETRY  if the read operation is not yet
		 *                            complete and must by tried again once
		 *                            external I/O has progressed
		 */
		inline Read_result read(At at, Byte_range_ptr const &dst);

		inline Write_result write(At at, Const_byte_range_ptr const &src);

		inline Resize_result resize(file_size num_bytes);

		/**
		 * Update the modification time of the file
		 */
		inline Update_mtime_result update_mtime(Timestamp t);

		/**
		 * Initiate or complete sync operation
		 */
		inline Sync_result sync();

		/**
		 * Inquiry whether a file (i.e., pipe) has new content to read
		 */
		inline Read_ready_result read_ready();

		/**
		 * Inquiry whether a file (i.e., pipe) has room for new content
		 */
		inline Write_ready_result write_ready();


		/*
		 * \noapi
		 * \deprecated
		 */
		struct { Read_ready_response_handler *response_handler_ptr = nullptr; };
};


Genode::Vfs::Read_result
Genode::Vfs::File_handle::read(At at, Byte_range_ptr const &dst)
{
	return _with_channel(
		[&] (Channel &channel) {
			return channel.read(at, dst).convert<Read_result>(
				[&] (size_t num_bytes) { return num_bytes; },
				[&] (Channel::Read_error e) {
					switch (e) {
					case Channel::Read_error::RETRY: return Read_error::RETRY;
					case Channel::Read_error::DENIED: break;
					}
					return Read_error::DENIED;
				});
		},
		[&] (Attach_error e) -> Read_result {
			return _converted<Read_error>(e);
		});
}


Genode::Vfs::Write_result
Genode::Vfs::File_handle::write(At at, Const_byte_range_ptr const &src)
{
	return _with_channel(
		[&] (Channel &channel) {
			_need_sync = true;
			return channel.write(at, src).convert<Write_result>(
				[&] (size_t num_bytes) {
					return num_bytes; },
				[&] (Channel::Write_error e) {
					switch (e) {
					case Channel::Write_error::RETRY: return Write_error::RETRY;
					case Channel::Write_error::DENIED: break;
					}
					return Write_error::DENIED;
				});
		},
		[&] (Attach_error e) -> Write_result {
			return _converted<Write_error>(e);
		});
}


Genode::Vfs::Resize_result Genode::Vfs::File_handle::resize(file_size num_bytes)
{
	if (!writeable)
		return Resize_result::DENIED;

	return _with_channel(
		[&] (Channel &channel) {
			_need_sync = true;
			switch (channel.resize(num_bytes)) {
			case Channel::Resize_result::OK:     return Resize_result::OK;
			case Channel::Resize_result::RETRY:  return Resize_result::RETRY;
			case Channel::Resize_result::DENIED: break;
			}
			return Resize_result::DENIED;
		},
		[&] (Attach_error e) -> Resize_result {
			return _converted<Resize_result>(e);
		});
}


Genode::Vfs::Update_mtime_result Genode::Vfs::File_handle::update_mtime(Timestamp t)
{
	if (!writeable)
		return Update_mtime_result::OK;

	return _with_channel(
		[&] (Channel &channel) {
			_need_sync = true;
			if (channel.update_mtime(t) == File_channel::Update_mtime_result::OK)
				return Update_mtime_result::OK;
			return Update_mtime_result::RETRY;
		},
		[&] (Attach_error e) -> Update_mtime_result {
			return _converted<Update_mtime_result>(e);
		});
}


Genode::Vfs::Sync_result Genode::Vfs::File_handle::sync()
{
	if (!_channel_ptr || !_need_sync)
		return Sync_result::OK;

	Sync_result const result = _channel_ptr->sync();

	if (result == Sync_result::OK) _need_sync = false;

	return result;
}


Genode::Vfs::Read_ready_result Genode::Vfs::File_handle::read_ready()
{
	_read_ready_requested = true;

	return _with_channel(
		[&] (Channel &channel) {
			if (channel.read_ready())
				return Read_ready_result::YES;

			channel.notify_read_ready();
			return Read_ready_result::RETRY;
		},
		[&] (Attach_error e) -> Read_ready_result {
			return _converted<Read_ready_result>(e);
		});
}


Genode::Vfs::Write_ready_result Genode::Vfs::File_handle::write_ready()
{
	return _with_channel(
		[&] (Channel &channel) {
			return channel.write_ready() ? Write_ready_result::YES
			                             : Write_ready_result::RETRY;
		},
		[&] (Attach_error e) -> Write_ready_result {
			return _converted<Write_ready_result>(e);
		});
}

#endif /* _INCLUDE__VFS__FILE_HANDLE_H_ */
