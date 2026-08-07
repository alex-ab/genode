/*
 * \brief  Representation of an open file
 * \author Norman Feske
 * \date   2011-02-17
 */

/*
 * Copyright (C) 2011-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__VFS_HANDLE_H_
#define _INCLUDE__VFS__VFS_HANDLE_H_

#include <vfs/directory_service.h>

namespace Genode::Vfs {
	struct Env;
	struct Read_ready_response_handler;
	class Vfs_handle;
	class File_system;
}


/**
 * Object for encapsulating application-level
 * response to VFS I/O
 *
 * These responses should be assumed to be called
 * during I/O signal dispatch.
 */
struct Genode::Vfs::Read_ready_response_handler : Interface
{
	/**
	 * Respond to a resource becoming readable
	 */
	virtual void read_ready_response() = 0;
};


class Genode::Vfs::Vfs_handle
{
	private:

		Directory_service &_ds;
		Allocator         &_alloc;
		int                _status_flags;

		Read_ready_response_handler *_handler_ptr = nullptr;

		/*
		 * Noncopyable
		 */
		Vfs_handle(Vfs_handle const &);
		Vfs_handle &operator = (Vfs_handle const &);

	public:

		class Guard
		{
			private:

				/*
				 * Noncopyable
				 */
				Guard(Guard const &);
				Guard &operator = (Guard const &);

				Vfs_handle * const _handle;

			public:

				Guard(Vfs_handle *handle) : _handle(handle) { }

				~Guard()
				{
					if (_handle)
						_handle->close();
				}
		};

		enum { STATUS_RDONLY = 0, STATUS_WRONLY = 1, STATUS_RDWR = 2 };

		Vfs_handle(Directory_service &ds,
		           Allocator         &alloc,
		           int                status_flags)
		:
			_ds(ds),
			_alloc(alloc),
			_status_flags(status_flags)
		{ }

		virtual ~Vfs_handle() { }

		Directory_service &ds() { return _ds; }
		Allocator      &alloc() { return _alloc; }


		int status_flags() const { return _status_flags; }
		void status_flags(int flags) { _status_flags = flags; }

		bool writeable() const
		{
			return (_status_flags & Directory_service::OPEN_MODE_ACCMODE) != STATUS_RDONLY;
		}

		/**
		 * Set response handler, unset with nullptr
		 */
		virtual void handler(Read_ready_response_handler *handler_ptr)
		{
			_handler_ptr = handler_ptr;
		}

		/**
		 * Apply to response handler if present
		 *
		 * XXX: may not be necesarry if the method above is virtual.
		 */
		void apply_handler(auto const &fn) const {
			if (_handler_ptr) fn(*_handler_ptr); }

		/**
		 * Notify application through response handler
		 */
		void read_ready_response() {
			if (_handler_ptr) _handler_ptr->read_ready_response(); }

		/**
		 * Close handle at backing file-system.
		 *
		 * This leaves the handle pointer in an invalid and unsafe state.
		 */
		inline void close() { ds().close(this); }


		/**************
		 ** File I/O **
		 **************/

		virtual Write_result write(At, Const_byte_range_ptr const &)
		{
			return Write_error::DENIED;
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
		virtual Read_result read(At, Byte_range_ptr const &dst) = 0;

		/**
		 * Return true if the handle has readable data
		 */
		virtual bool read_ready() const = 0;

		/**
		 * Return true if the handle might accept a write operation
		 */
		virtual bool write_ready() const = 0;

		/**
		 * Explicitly indicate interest in read-ready for a handle
		 *
		 * For example, the file-system-session plugin can then send READ_READY
		 * packets to the server.
		 *
		 * \return false if notification setup failed
		 */
		virtual bool notify_read_ready() { return true; }

		virtual Ftruncate_result ftruncate(file_size)
		{
			return FTRUNCATE_ERR_NO_PERM;
		}

		/**
		 * Initiate or complete sync operation
		 */
		virtual Sync_result sync() { return Sync_result::OK; }

		/**
		 * Update the modification time of a file
		 *
		 * \return true if update attempt was successful
		 */
		virtual bool update_modification_timestamp(Timestamp)
		{
			return true;
		}
};

#endif /* _INCLUDE__VFS__VFS_HANDLE_H_ */
