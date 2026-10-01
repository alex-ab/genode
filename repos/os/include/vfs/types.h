/*
 * \brief  Types used by VFS
 * \author Norman Feske
 * \date   2014-04-07
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__TYPES_H_
#define _INCLUDE__VFS__TYPES_H_

#include <util/string.h>
#include <util/progress.h>
#include <util/allocation.h>
#include <util/list_model.h>
#include <base/node.h>
#include <base/env.h>
#include <base/signal.h>
#include <base/allocator.h>
#include <dataspace/client.h>
#include <os/path.h>

namespace Genode::Vfs {

	enum { MAX_PATH_LEN = 512 };

	using file_size = unsigned long long;

	struct Timestamp { uint64_t ms_since_1970; };

	enum class Dirent_type { DIRECTORY, SYMLINK, FILE, DEVICE };

	struct Node_rwx
	{
		bool readable;
		bool writeable;
		bool executable;

		static Node_rwx ro()  { return { .readable   = true,
		                                 .writeable  = false,
		                                 .executable = false }; }

		static Node_rwx wo()  { return { .readable   = false,
		                                 .writeable  = true,
		                                 .executable = false }; }

		static Node_rwx rw()  { return { .readable   = true,
		                                 .writeable  = true,
		                                 .executable = false }; }

		static Node_rwx rx()  { return { .readable   = true,
		                                 .writeable  = false,
		                                 .executable = true }; }

		static Node_rwx rwx() { return { .readable   = true,
		                                 .writeable  = true,
		                                 .executable = true }; }
	};

	namespace File
	{
		enum class Read  { NOTHING, ANYWHERE };
		enum class Write { DENIED, CONTINUOUS, TRANSACTIONAL };
	};

	/**
	 * Seek position in bytes, for read and write operations
	 */
	struct At { file_size pos; };

	using Absolute_path = Path<MAX_PATH_LEN>;

	struct Scanner_policy_path_element
	{
		static bool identifier_char(char c, unsigned /* i */)
		{
			return (c != '/') && (c != 0);
		}

		static bool end_of_quote(const char *s)
		{
			return s[0] != '\\' && s[1] == '\"';
		}
	};

	struct Parent_fs : Noncopyable, Interface
	{
		virtual void notify_watchers(Span const &) = 0;
		virtual void detach_channels(Span const &) = 0;
	};

	static inline void with_compound_dir(Span const &path, auto const &fn)
	{
		char const *s = path.start; size_t n = path.num_bytes;

		/* if path is directory, drop trailing slash, ignore multiple slashes */
		while (n > 0 && s[n - 1] == '/') n--;

		/* search from end to front for the slash of the compound directory */
		while (n > 0 && s[n - 1] != '/') n--;

		if (n) fn(Span(s, n));
	}

	static inline void for_each_path_elem(Span const &path, auto const &fn)
	{
		path.split('/', [&] (Span const &elem) {
			if (elem.num_bytes)
				fn(elem); });
	}

	static inline Span without_last_path_elem(Span const &path)
	{
		size_t n = path.num_bytes;
		while (n > 0 && path.start[n - 1] != '/') n--;
		return { path.start, n };
	}

	using Watch_result = Attempt<Ok, Alloc_error>;

	enum class Write_error { RETRY, DENIED, OUT_OF_RAM, OUT_OF_CAPS };

	using Write_result = Attempt<size_t, Write_error>;

	enum class Read_error { RETRY, DENIED, OUT_OF_RAM, OUT_OF_CAPS };

	using Read_result = Attempt<size_t, Read_error>;

	struct Read_eof : Read_result { Read_eof() : Read_result(0) { }; };

	enum class Resize_result { OK, RETRY, DENIED, OUT_OF_RAM, OUT_OF_CAPS };

	enum class Sync_result { OK, RETRY };

	enum class Update_mtime_result { OK, RETRY, DENIED, OUT_OF_RAM, OUT_OF_CAPS };

	enum class Mkdir_result   { CREATED, UPDATED, RETRY, DENIED };

	enum class Symlink_result { CREATED, UPDATED, RETRY, DENIED };

	enum class Unlink_result { OK, DENIED };

	enum class Rename_result { OK, DENIED };

	enum class Stat_result   { OK, DENIED };

	struct Path_elem
	{
		unsigned index;

		/**
		 * Return true if index matches last 'path' element
		 */
		bool last(Span const &path) const
		{
			unsigned total = 0;
			for_each_path_elem(path, [&] (Span const &) { total++; });
			return (index + 1) == total;
		}
	};

	enum class Follow_error { RETRY, NO_SYMLINK, DENIED };

	using Follow_result = Attempt<Path_elem, Follow_error>;

	/**
	 * Walk path in search of the first 'Path_elem' that satisfies 'cond_fn'
	 *
	 * The functor 'fn' is called for the first matching partial path.
	 * If 'cond_fn' never applies, NO_SYMLINK is returned.
	 */
	static inline Follow_result follow_path(Span const &path,
	                                        auto const &cond_fn, auto const &fn)
	{
		String<MAX_PATH_LEN> partial_path { "" };
		Path_elem path_elem { };
		bool found = false;

		for_each_path_elem(path, [&] (Span const &elem) {
			if (found)
				return;

			partial_path = { partial_path, "/", elem };
			if (cond_fn(partial_path)) {
				found = true;
				return;
			}
			path_elem.index++;
		});

		if (found)
			return fn(path_elem, partial_path);

		return Follow_error::NO_SYMLINK;
	}

	/**
	 * Interface to notify VFS user once a file channel becomes ready to read
	 *
	 * Note that 'read_ready_response' is called at I/O signal level.
	 */
	struct Read_ready_response_handler : Interface
	{
		virtual void read_ready_response() = 0;
	};

	enum class Read_ready_result  { YES, RETRY, DENIED, OUT_OF_RAM, OUT_OF_CAPS };

	enum class Write_ready_result { YES, RETRY, DENIED, OUT_OF_RAM, OUT_OF_CAPS };

	struct File_channel;

	enum class Open_error { RETRY, DENIED, OUT_OF_RAM, OUT_OF_CAPS };

	using Open_result = Unique_attempt<File_channel &, Open_error>;

	struct Dir_channel;

	enum class Opendir_error { RETRY, DENIED, OUT_OF_RAM, OUT_OF_CAPS };

	using Opendir_result = Unique_attempt<Dir_channel &, Opendir_error>;

	template <typename TO, typename FROM>
	static inline TO converted_error(FROM e)
	{
		switch (e) {
		case FROM::RETRY:       return TO::RETRY;
		case FROM::DENIED:      return TO::DENIED;
		case FROM::OUT_OF_RAM:  return TO::OUT_OF_RAM;
		case FROM::OUT_OF_CAPS: return TO::OUT_OF_CAPS;
		}
		return TO::DENIED;
	}

	struct Env;
	struct Root;
}


struct Genode::Vfs::File_channel : Noncopyable, Interface
{
	bool const writeable;

	struct Attr { bool writeable; };

	File_channel(Attr attr) : writeable(attr.writeable) { }

	/**
	 * Define read-ready response handler, called by VFS user
	 */
	virtual void handler(Read_ready_response_handler &handler)
	{
		_handler_ptr = &handler;
	}

	/**
	 * Schedule read-ready notification, called by the VFS user
	 */
	virtual void notify_read_ready() { }

	/**
	 * Notify application that a read operation can be retried
	 *
	 * Called by the 'File_system' implementation.
	 */
	void read_ready_response()
	{
		if (_handler_ptr) _handler_ptr->read_ready_response();
	}

	/**
	 * Return true whenever the channel has readable data
	 */
	virtual bool read_ready() const = 0;

	/**
	 * Return true whenever the channel accepts data to write
	 */
	virtual bool write_ready() const = 0;

	/*
	 * Result types excluding OUT_OF_RAM and OUT_OF_CAPS
	 *
	 * The allocation errors OUT_OF_RAM and OUT_OF_CAPS are only expected
	 * at channel-creation time.
	 */

	enum class Write_error { RETRY, DENIED };

	using Write_result = Attempt<size_t, Write_error>;

	/**
	 * Initiate or complete write operation
	 *
	 * On success, the method returns the number of consumed bytes.
	 *
	 * Note that the consumed content is not known to be physically stored
	 * when the method returns. The data could be held in an intermediate
	 * buffer such as the packet-stream buffer of a file-system session.
	 * Use 'sync' to observe the completion of write operations.
	 */
	virtual Write_result write(At, Const_byte_range_ptr const &)
	{
		return Write_error::DENIED;
	}

	/**
	 * Initiate or complete sync operation
	 */
	virtual Sync_result sync() { return Sync_result::OK; }

	enum class Read_error { RETRY, DENIED };

	using Read_result = Attempt<size_t, Read_error>;

	struct Read_eof : Read_result { Read_eof() : Read_result(0) { }; };

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

	enum class Resize_result { OK, RETRY, DENIED };

	virtual Resize_result resize(file_size) { return Resize_result::DENIED; }

	enum class Update_mtime_result { OK, RETRY };

	/**
	 * Update the modification time of a file
	 *
	 * Note that the return value does not reflect whether the modification
	 * time is captured and held by the targeted file system. A file system
	 * that discards the information still returns OK. Typical scenarios
	 * where the modification time is updated as a side effect, like when a
	 * modified file is closed, would not reflect this condition to the
	 * application-level anyway. In other cases where the integrity of
	 * modification times is assumed, a subsequent 'stat' shall be used
	 * confirm the effect of the update.
	 */
	virtual Update_mtime_result update_mtime(Timestamp)
	{
		return Update_mtime_result::OK;
	}

	virtual void destruct() = 0;

	private:

		struct { Read_ready_response_handler *_handler_ptr = nullptr; };
};


struct Genode::Vfs::Dir_channel : Interface, Noncopyable
{
	enum class Read_error { RETRY, DENIED };

	using Read_result = Attempt<size_t, Read_error>;

	struct Read_eof : Read_result { Read_eof() : Read_result(0) { }; };

	/**
	 * Initiate or complete read of directory entries
	 *
	 * On success, the method returns the number of read bytes.
	 * If zero, the end of file is reached.
	 *
	 * \return Read_error::RETRY  if the read operation is not yet
	 *                            complete and must by tried again once
	 *                            external I/O has progressed
	 */
	virtual Read_result read(At, Byte_range_ptr const &dst) = 0;

	virtual void destruct() = 0;
};

#endif /* _INCLUDE__VFS__TYPES_H_ */
