/*
 * \brief  Internal nodes of VFS server
 * \author Emery Hemingway
 * \author Christian Helmuth
 * \author Norman Feske
 * \date   2016-03-29
 */

/*
 * Copyright (C) 2016-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _VFS__NODE_H_
#define _VFS__NODE_H_

/* Genode includes */
#include <vfs/file_system.h>
#include <os/path.h>
#include <base/id_space.h>

/* Local includes */
#include "assert.h"

namespace Vfs_server {

	using namespace File_system;
	using namespace Genode;
	using namespace Genode::Vfs;

	using Packet_stream = ::File_system::Session::Tx::Sink;

	class Node_base;
	class Io_node;
	class Watch_node;
	class Directory;
	class File;
	class Symlink;

	using Node_space = Id_space<Node_base>;
	using Node_queue = Fifo<Node_base>;

	/* Vfs::MAX_PATH is shorter than File_system::MAX_PATH */
	enum { MAX_PATH_LEN = Vfs::MAX_PATH_LEN };

	using Path = Genode::Path<MAX_PATH_LEN>;
	using Packet_descriptor = File_system::Packet_descriptor;

	using Watch_handle = File_system::Watch_handle;
	using File_handle  = File_system::File_handle;

	struct Payload_ptr
	{
		char *_ptr;

		void with_bytes(Packet_descriptor const &packet, auto const &fn)
		{
			if (_ptr || packet.length() == 0)
				fn(Byte_range_ptr(_ptr, packet.length()));
			else
				warning("payload ptr unexpectedly not defined");
		}
	};

	/**
	 * Type trait for determining the node type for a given handle type
	 */
	template<typename T> struct Node_type;
	template<> struct Node_type<Node_handle>    { using Type = Io_node; };
	template<> struct Node_type<Dir_handle>     { using Type = Directory; };
	template<> struct Node_type<File_handle>    { using Type = File; };
	template<> struct Node_type<Symlink_handle> { using Type = Symlink; };
	template<> struct Node_type<Watch_handle>   { using Type = Watch_node; };

	/**
	 * Type trait for determining the handle type for a given node type
	 */
	template<typename T> struct Handle_type;
	template<> struct Handle_type<Io_node>   { using Type = Node_handle; };
	template<> struct Handle_type<Directory> { using Type = Dir_handle; };
	template<> struct Handle_type<File>      { using Type = File_handle; };
	template<> struct Handle_type<Symlink>   { using Type = Symlink_handle; };
	template<> struct Handle_type<Watch>     { using Type = Watch_handle; };
}


class Vfs_server::Node_base : Node_space::Element, Node_queue::Element
{
	public:

		Path const path;

	protected:

		/**
		 * Packet descriptor to be added to the acknowledgement queue
		 *
		 * The '_acked_packet' is reset by 'submit_job' and assigned
		 * to a valid descriptor by 'try_execute_job'. The validity of the
		 * packet descriptor is tracked by '_acked_packet_valid'.
		 */
		Packet_descriptor _acked_packet { };

		bool _acked_packet_valid = false;

		bool _packet_in_progress = false;

		bool _modified = false;

		enum class Read_ready_state { DONT_CARE, REQUESTED, READY };

		Read_ready_state _read_ready_state { Read_ready_state::DONT_CARE };

	public:

		friend Node_queue;
		using Node_queue::Element::enqueued;

		Node_base(Node_space &space, char const *node_path)
		:
			Node_space::Element(*this, space), path(node_path)
		{ }

		virtual ~Node_base() { }

		using Node_space::Element::id;

		enum class Submit_result { DENIED, ACCEPTED, STALLED };

		bool job_in_progress() const { return _packet_in_progress; }

		/**
		 * Return true if node is ready to accept 'submit_job'
		 *
		 * Each node can deal with only one job at a time, except for file
		 * nodes, which accept a job in addition to an already submitted
		 * READ_READY request (which leaves '_packet_in_progress' untouched).
		 */
		bool job_acceptable() const { return !job_in_progress()
		                                  && !acknowledgement_pending(); }

		/**
		 * Submit job to node
		 *
		 * When called, the node is expected to be idle (neither queued in
		 * the active-nodes queue nor the finished-nodes queue).
		 */
		virtual Submit_result submit_job(Packet_descriptor, Payload_ptr)
		{
			return Submit_result::DENIED;
		}

		/**
		 * Execute submitted job
		 *
		 * This function must not be called if 'job_in_progress()' is false.
		 */
		virtual void execute_job()
		{
			warning("Node_base::execute_job unexpectedly called");
		}

		/**
		 * Return true if the node has at least one acknowledgement ready
		 */
		bool acknowledgement_pending() const
		{
			return (_read_ready_state == Read_ready_state::READY)
			     || _acked_packet_valid;
		}

		bool active() const
		{
			return acknowledgement_pending()
			    || job_in_progress()
			    || (_read_ready_state == Read_ready_state::REQUESTED);
		}

		/**
		 * Return and consume one pending acknowledgement
		 */
		Packet_descriptor dequeue_acknowledgement()
		{
			if (_read_ready_state == Read_ready_state::READY) {
				_read_ready_state =  Read_ready_state::DONT_CARE;

				Packet_descriptor ack_read_ready(Packet_descriptor(),
				                                 Node_handle { id().value },
				                                 Packet_descriptor::READ_READY,
				                                 0, 0);
				ack_read_ready.succeeded(true);
				return ack_read_ready;
			}

			if (_acked_packet_valid) {
				_acked_packet_valid = false;
				return _acked_packet;
			}

			warning("dequeue_acknowledgement called with no pending ack");
			return Packet_descriptor();
		}

		/**
		 * Return true if node was written to
		 */
		bool modified() const { return _modified; }

		/**
		 * Print for debugging
		 */
		void print(Output &out) const
		{
			Genode::print(out, path.string(), " (id=", id(), ")");
		}
};


/**
 * Super-class for nodes that process read/write packets
 */
class Vfs_server::Io_node : public Vfs_server::Node_base
{
	private:

		Mode const _mode;

	protected:

		/**
		 * Current job of this node, assigned by 'submit_job'
		 */
		Packet_descriptor _packet { };

		Payload_ptr _payload_ptr { }; /* pointer into current packet buffer */

		void _import_packet(Packet_descriptor const &packet)
		{
			if (job_in_progress())
				error("job unexpectedly submitted to busy node");

			_packet             = packet;
			_acked_packet_valid = false;
			_acked_packet       = Packet_descriptor { };
		}

		void _ack_and_reset_packet(Payload_ptr &ptr)
		{
			_packet_in_progress = false;
			_acked_packet_valid = true;
			_acked_packet       = _packet;

			_packet = { };
			ptr     = { };
		}

		void _ack_successful_packet(size_t count, Payload_ptr &ptr)
		{
			_ack_and_reset_packet(ptr);
			_acked_packet.length(count);
			_acked_packet.succeeded(true);

		}

		void _ack_failed_packet(Payload_ptr &ptr)
		{
			_ack_and_reset_packet(ptr);
			_acked_packet.succeeded(false);
		}

	protected:

		Submit_result _accept()
		{
			_packet_in_progress = true;
			return Submit_result::ACCEPTED;
		}

		Submit_result _submit_read()
		{
			return (_mode & READ_ONLY) ? _accept() : Submit_result::DENIED;
		}

		Submit_result _submit_write()
		{
			return (_mode & WRITE_ONLY) ? _accept() : Submit_result::DENIED;
		}

		Submit_result _submit_sync() { return _accept(); }

		Submit_result _submit_write_timestamp()
		{
			return (_mode & WRITE_ONLY) ? _accept() : Submit_result::DENIED;
		}

		void _execute_mtime(Vfs_handle &vfs_handle)
		{
			_packet.with_timestamp([&] (::File_system::Timestamp const time) {
				Vfs::Timestamp ts { .ms_since_1970 = time.ms_since_1970 };
				vfs_handle.update_modification_timestamp(ts);
			});
			_ack_successful_packet(0, _payload_ptr);

			_modified = true;
		}

		void _execute_sync(Vfs_handle &vfs_handle)
		{
			if (vfs_handle.sync() == Sync_result::OK)
				_ack_successful_packet(0, _payload_ptr);
		}

	public:

		Io_node(Node_space &space, char const *path, Mode mode)
		:
			Node_base(space, path), _mode(mode)
		{ }

		virtual ~Io_node() { }

		using Node_space::Element::id;

		Mode mode() const { return _mode; }

		/**
		 * Vfs_server::Node_base interface
		 */
		void execute_job() override { }
};


class Vfs_server::Watch_node final : public Vfs_server::Node_base,
                                     public Vfs::Watch_handle::Handler
{
	public:

		struct Watch_node_response_handler : Interface
		{
			virtual void handle_watch_node_response(Watch_node &) = 0;
		};

	private:

		/*
		 * Noncopyable
		 */
		Watch_node(Watch_node const &);
		Watch_node &operator = (Watch_node const &);

		Vfs::Watch_handle _watch_handle;

		Watch_node_response_handler &_watch_node_response_handler;

	public:

		Watch_node(Node_space                  &space,
		           Vfs::Watch_handles          &handles,
		           Vfs::File_system            &root_dir,
		           char                  const *path,
		           Watch_node_response_handler &watch_node_response_handler)
		:
			Node_base(space, path),
			_watch_handle(handles, root_dir, path, *this),
			_watch_node_response_handler(watch_node_response_handler)
		{ }

		Watch_result watch() { return _watch_handle.watch(); }

		/**
		 * Vfs::Watch_handle::Handler interface
		 */
		void io_handle_watch() override
		{
			_acked_packet = Packet_descriptor(Packet_descriptor(),
			                                  Node_handle { id().value },
			                                  Packet_descriptor::CONTENT_CHANGED,
			                                  0, 0);
			_acked_packet.succeeded(true);
			_acked_packet_valid = true;
			_watch_node_response_handler.handle_watch_node_response(*this);
		}

		/**
		 * Vfs_server::Node_base interface
		 */
		Submit_result submit_job(Packet_descriptor, Payload_ptr) override
		{
			/*
			 * This can only happen if a client misbehaves by submitting
			 * work to a watch handle.
			 */
			warning("job unexpectedly submitted to watch handle");

			/* don't reset '_acked_packet' as defined in the constructor */

			return Submit_result::DENIED;
		}
};


struct Vfs_server::Symlink : Io_node
{
	private:

		Vfs::Vfs_handle &_handle;

		using Write_buffer = String<MAX_PATH_LEN + 1>;

		Write_buffer _write_buffer { };

		bool _partial_operation() const
		{
			/* partial read or write is not supported */
			if (_packet.position() != 0) {
				warning("attempt for partial operation on a symlink");
				return true;
			}
			return false;
		}

		bool _max_path_length_exceeded() const
		{
			return _packet.length() >= MAX_PATH_LEN;
		}

		static Vfs_handle &_open(Vfs::File_system  &vfs, Allocator &alloc,
		                         char const *path, bool create)
		{
			Vfs_handle *h = nullptr;
			assert_openlink(vfs.openlink(path, create, &h, alloc));
			return *h;
		}

		void _execute_read()
		{
			_payload_ptr.with_bytes(_packet, [&] (Byte_range_ptr const &dst) {
				_handle.read({ }, dst).with_result(
					[&] (size_t num_bytes) {
						_ack_successful_packet(num_bytes, _payload_ptr);
					},
					[&] (Vfs_handle::Read_error e) {
						if (e != Vfs_handle::Read_error::RETRY)
							_ack_failed_packet(_payload_ptr);
					}); });
		}

		void _execute_write()
		{
			/*
			 * Write symlink content from '_write_buffer' instead of the
			 * '_payload_ptr'. In contrast to '_payload_ptr', which points
			 * to shared memory, the null-termination of the content of
			 * '_write_buffer' does not depend on the goodwill of the client.
			 */
			Const_byte_range_ptr const src { _write_buffer.string(),
			                                 _write_buffer.length() };
			size_t out_count = 0;
			_handle.write({ }, src).with_result(
				[&] (size_t num_bytes) {
					out_count = num_bytes;
					_modified = true;
				},
				[&] (Vfs_handle::Write_error e) {
					switch (e) {
					case Vfs_handle::Write_error::RETRY:  break;
					case Vfs_handle::Write_error::DENIED:
						_ack_failed_packet(_payload_ptr); break;
					}
				});

			if (out_count == src.num_bytes)
				_ack_successful_packet(src.num_bytes, _payload_ptr);
			else
				_ack_failed_packet(_payload_ptr);
		}

	public:

		Symlink(Node_space       &space,
		        Vfs::File_system &vfs,
		        Allocator        &alloc,
		        char       const *path,
		        Mode              mode,
		        bool              create)
		:
			Io_node(space, path, mode),
			_handle(_open(vfs, alloc, path, create))
		{ }

		~Symlink() { _handle.close(); }

		Submit_result submit_job(Packet_descriptor packet, Payload_ptr ptr) override
		{
			_import_packet(packet);
			_payload_ptr = ptr;

			switch (packet.operation()) {

			case Packet_descriptor::READ:

				if (_partial_operation())
					return Submit_result::DENIED;

				return _submit_read();

			case Packet_descriptor::WRITE:
				{
					if (_partial_operation() || _max_path_length_exceeded())
						return Submit_result::DENIED;

					/* accessed by 'execute_job' */
					_payload_ptr.with_bytes(packet, [&] (Byte_range_ptr const &bytes) {
						_write_buffer = { Cstring(bytes.start, bytes.num_bytes) };
					});
					return _submit_write();
				}

			case Packet_descriptor::SYNC:            return _submit_sync();
			case Packet_descriptor::READ_READY:      return Submit_result::DENIED;
			case Packet_descriptor::CONTENT_CHANGED: return Submit_result::DENIED;
			case Packet_descriptor::WRITE_TIMESTAMP: return _submit_write_timestamp();
			}

			warning("invalid operation ", (int)_packet.operation(), " "
			        "requested from symlink node");

			return Submit_result::DENIED;
		}

		void execute_job() override
		{
			switch (_packet.operation()) {

			case Packet_descriptor::WRITE:           _execute_write(); break;
			case Packet_descriptor::READ:            _execute_read();  break;
			case Packet_descriptor::SYNC:            _execute_sync (_handle); break;
			case Packet_descriptor::WRITE_TIMESTAMP: _execute_mtime(_handle); break;

			/* never executed */
			case Packet_descriptor::READ_READY:
			case Packet_descriptor::CONTENT_CHANGED:
				break;
			}
		}
};


class Vfs_server::File : public Io_node, public Vfs::Read_ready_response_handler
{
	private:

		Vfs::Vfs_handle &_handle;

		using Stat = Directory_service::Stat;

		bool _warned_once = false;

		seek_off_t _seek_pos()
		{
			seek_off_t seek_pos = _packet.position();

			if (seek_pos == (seek_off_t)SEEK_TAIL) {
				seek_pos = ~0UL;
				if (!_warned_once)
					error("SEEK_TAIL as seek position is unsupported");
				_warned_once = true;
			}
			return seek_pos;
		}

		enum class Write_type { UNKNOWN, CONTINUOUS, TRANSACTIONAL };

		Write_type _write_type = Write_type::UNKNOWN;

		/**
		 * Number of bytes consumed by VFS write
		 *
		 * Used for the incremental write to continuous files.
		 */
		size_t _write_pos = 0;

		bool _watch_read_ready = false;

		void _execute_write()
		{
			_payload_ptr.with_bytes(_packet, [&] (Byte_range_ptr src) {
				src.with_skipped_bytes(_write_pos, [&] (Byte_range_ptr const &src) {

					At const at { _seek_pos() + _write_pos };

					size_t out_count = 0;

					_handle.write(at, Span(src.start, src.num_bytes)).with_result(
						[&] (size_t num_bytes) {
							out_count = num_bytes;
							_modified = true;
						},
						[&] (Vfs_handle::Write_error e) {
							switch (e) {
							case Vfs_handle::Write_error::RETRY:  break;
							case Vfs_handle::Write_error::DENIED:
								_ack_failed_packet(_payload_ptr); break;
							}
						});

					if (out_count == src.num_bytes) {
						_ack_successful_packet(src.num_bytes, _payload_ptr);
						return;
					}

					/*
					 * The write request was only partially successful.
					 * Continue writing if the file is continuous.
					 * Return an error if the file is transactional.
					 */
					if (_write_type == Write_type::TRANSACTIONAL) {
						_ack_failed_packet(_payload_ptr);
						return;
					}

					/*
					 * Keep executing the write operation for the remaining
					 * bytes.
					 */
					_write_pos += out_count;
				});
			});
		}

		void _execute_read()
		{
			_payload_ptr.with_bytes(_packet, [&] (Byte_range_ptr const &dst) {
				_handle.read(At { _seek_pos() }, dst).with_result(
					[&] (size_t num_bytes) {
						_ack_successful_packet(num_bytes, _payload_ptr);
					},
					[&] (Vfs_handle::Read_error e) {
						if (e != Vfs_handle::Read_error::RETRY)
							_ack_failed_packet(_payload_ptr);
					}); });
		}

		Submit_result _submit_read_ready()
		{
			_read_ready_state = Read_ready_state::REQUESTED;

			if (_handle.read_ready()) {
				/* if the handle is ready, send a packet back immediately */
				read_ready_response();
			} else {
				/* register to send READ_READY acknowledgement later */
				_handle.notify_read_ready();
			}
			return Submit_result::ACCEPTED;
		}

		static Vfs_handle &_open(Vfs::File_system  &vfs, Allocator &alloc,
		                         char const *path, Mode mode, bool create)
		{
			Vfs_handle *h = nullptr;
			unsigned vfs_mode = (mode-1) |
				(create ? Vfs::Directory_service::OPEN_MODE_CREATE : 0);

			assert_open(vfs.open(path, vfs_mode, &h, alloc));
			return *h;
		}

	public:

		File(Node_space       &space,
		     Vfs::File_system &vfs,
		     Allocator        &alloc,
		     char       const *path,
		     Mode              mode,
		     bool              create)
		:
			Io_node(space, path, mode),
			_handle(_open(vfs, alloc, path, mode, create))
		{
			_handle.handler(this); // XXX remove?

			if (mode == Mode::WRITE_ONLY || mode == Mode::READ_WRITE) {
				using Result = Directory_service::Stat_result;
				Vfs::Directory_service::Stat stat { };
				if (vfs.stat(path, stat) == Result::STAT_OK)
					_write_type = (stat.type == Vfs::Node_type::CONTINUOUS_FILE)
					            ? Write_type::CONTINUOUS : Write_type::TRANSACTIONAL;
			}
		}

		~File()
		{
			_handle.handler(nullptr);
			_handle.close();
		}

		void truncate(file_size_t size)
		{
			(void)_handle.ftruncate(size);
		}

		Submit_result submit_job(Packet_descriptor packet, Payload_ptr ptr) override
		{
			/*
			 * Accept a READ_READY request without occupying '_packet'.
			 * This way, another request can follow a READ_READY request
			 * without blocking on the completion of READ_READY.
			 */
			if (packet.operation() == Packet_descriptor::READ_READY) {
				_read_ready_state = Read_ready_state::REQUESTED;
			} else {
				_import_packet(packet);
				_payload_ptr = ptr;
			}

			_write_type = Write_type::UNKNOWN;
			_write_pos  = 0;

			switch (packet.operation()) {
			case Packet_descriptor::READ:            return _submit_read();
			case Packet_descriptor::WRITE:           return _submit_write();
			case Packet_descriptor::SYNC:            return _submit_sync();
			case Packet_descriptor::READ_READY:      return _submit_read_ready();
			case Packet_descriptor::CONTENT_CHANGED: return Submit_result::DENIED;
			case Packet_descriptor::WRITE_TIMESTAMP: return _submit_write_timestamp();
			}

			warning("invalid operation ", (int)_packet.operation(), " "
			        "requested from file node");

			return Submit_result::DENIED;
		}

		void execute_job() override
		{
			switch (_packet.operation()) {

			case Packet_descriptor::WRITE:           _execute_write(); break;
			case Packet_descriptor::READ:            _execute_read();  break;
			case Packet_descriptor::SYNC:            _execute_sync (_handle); break;
			case Packet_descriptor::WRITE_TIMESTAMP: _execute_mtime(_handle); break;

			/* never executed */
			case Packet_descriptor::READ_READY:
			case Packet_descriptor::CONTENT_CHANGED:
				break;
			}
		}

		/**
		 * Vfs::Io_response_handler interface
		 *
		 * Called by the VFS plugin of this handle
		 */
		void read_ready_response() override
		{
			if (_read_ready_state == Read_ready_state::REQUESTED)
				_read_ready_state =  Read_ready_state::READY;
		}
};


struct Vfs_server::Directory : Io_node
{
	public:

		enum class Session_writeable { READ_ONLY, WRITEABLE };

	private:

		Vfs::Vfs_handle &_handle;

		Session_writeable const _writeable;

		using Vfs_dirent = Directory_service::Dirent;
		using Fs_dirent  = ::File_system::Directory_entry;

		bool _position_and_length_aligned_with_dirent_size()
		{
			if (_packet.length() < sizeof(Directory_entry))
				return false;

			if (_packet.length() % sizeof(::File_system::Directory_entry))
				return false;

			if (_packet.position() % sizeof(::File_system::Directory_entry))
				return false;

			return true;
		}

		static Fs_dirent _convert_dirent(Vfs_dirent from, Session_writeable writeable)
		{
			from.sanitize();

			auto fs_dirent_type = [&] (Vfs::Directory_service::Dirent_type type)
			{
				using From = Vfs::Directory_service::Dirent_type;
				using To   = ::File_system::Node_type;

				/*
				 * This should never be taken because 'END' is checked as a
				 * precondition prior the call to of this function.
				 */
				To const default_result = To::CONTINUOUS_FILE;

				switch (type) {
				case From::END:                return default_result;
				case From::DIRECTORY:          return To::DIRECTORY;
				case From::SYMLINK:            return To::SYMLINK;
				case From::CONTINUOUS_FILE:    return To::CONTINUOUS_FILE;
				case From::TRANSACTIONAL_FILE: return To::TRANSACTIONAL_FILE;
				}
				return default_result;
			};

			return {
				.type = fs_dirent_type(from.type),
				.rwx  = {
					.readable   = from.rwx.readable,
					.writeable  = (writeable == Session_writeable::WRITEABLE)
					            ? from.rwx.writeable : false,
					.executable = from.rwx.executable },
				.name = { from.name.buf }
			};
		}

		/**
		 * Convert VFS directory entry to FS directory entry in place in the
		 * payload buffer
		 *
		 * \return  size of converted data in bytes
		 */
		size_t _convert_vfs_dirents_to_fs_dirents(Byte_range_ptr const &buf)
		{
			static_assert(sizeof(Vfs_dirent) == sizeof(Fs_dirent));

			size_t const step = sizeof(Fs_dirent);

			size_t converted_length = 0;

			for (file_size offset = 0; offset + step <= buf.num_bytes; offset += step) {

				char * const ptr = buf.start + offset;

				Vfs_dirent &vfs_dirent = *(Vfs_dirent *)(ptr);
				Fs_dirent  &fs_dirent  = *(Fs_dirent  *)(ptr);

				if (vfs_dirent.type == Vfs::Directory_service::Dirent_type::END)
					break;

				fs_dirent = _convert_dirent(vfs_dirent, _writeable);

				converted_length += step;
			}

			return converted_length;
		}

		static Vfs_handle &_open(Vfs::File_system &vfs, Allocator &alloc,
		                         char const *path, bool create)
		{
			Vfs_handle *h = nullptr;
			assert_opendir(vfs.opendir(path, create, &h, alloc));
			return *h;
		}

	public:

		Directory(Node_space       &space,
		          Vfs::File_system &vfs,
		          Allocator        &alloc,
		          char const       *path,
		          bool              create,
		          Session_writeable writeable)
		:
			Io_node(space, path, READ_ONLY),
			_handle(_open(vfs, alloc, path, create)), _writeable(writeable)
		{ }

		~Directory() { _handle.close(); }

		/**
		 * Open a file handle at this directory
		 */
		Node_space::Id file(Node_space       &space,
		                    Vfs::File_system &vfs,
		                    Allocator        &alloc,
		                    char const       *path,
		                    Mode              mode,
		                    bool              create)
		{
			File &file = *new (alloc)
				File(space, vfs, alloc,
				     Path(path, Node_base::path.string()).base(), mode, create);

			return file.id();
		}

		/**
		 * Open a symlink handle at this directory
		 */
		Node_space::Id symlink(Node_space       &space,
		                       Vfs::File_system &vfs,
		                       Allocator        &alloc,
		                       char       const *path,
		                       Mode              mode,
		                       bool              create)
		{
			Symlink &link = *new (alloc)
				Symlink(space, vfs, alloc,
				        Path(path, Node_base::path.string()).base(), mode, create);

			return link.id();
		}

		Submit_result submit_job(Packet_descriptor packet, Payload_ptr ptr) override
		{
			_import_packet(packet);
			_payload_ptr = ptr;

			switch (packet.operation()) {

			case Packet_descriptor::READ:

				if (!_position_and_length_aligned_with_dirent_size())
					return Submit_result::DENIED;

				return _submit_read();

			case Packet_descriptor::WRITE:           return Submit_result::DENIED;
			case Packet_descriptor::SYNC:            return _submit_sync();
			case Packet_descriptor::READ_READY:      return Submit_result::DENIED;
			case Packet_descriptor::CONTENT_CHANGED: return Submit_result::DENIED;
			case Packet_descriptor::WRITE_TIMESTAMP: return _submit_write_timestamp();
			}

			warning("invalid operation ", (int)_packet.operation(), " "
			        "requested from directory node");
			return Submit_result::DENIED;
		}

		void execute_job() override
		{
			switch (_packet.operation()) {

			case Packet_descriptor::READ:
				_payload_ptr.with_bytes(_packet, [&] (Byte_range_ptr const &dst) {
					_handle.read(At { _packet.position() }, dst).with_result(
						[&] (size_t num_bytes) {
							Byte_range_ptr bytes { dst.start, num_bytes };
							size_t n = _convert_vfs_dirents_to_fs_dirents(bytes);
							_ack_successful_packet(n, _payload_ptr);
						},
						[&] (Vfs_handle::Read_error e) {
							if (e != Vfs_handle::Read_error::RETRY)
								_ack_failed_packet(_payload_ptr);
						});
				});
				break;

			case Packet_descriptor::WRITE_TIMESTAMP: _execute_mtime(_handle); break;

			/* never executed */
			case Packet_descriptor::SYNC:
			case Packet_descriptor::WRITE:
			case Packet_descriptor::READ_READY:
			case Packet_descriptor::CONTENT_CHANGED:
				break;
			}
		}
};

#endif /* _VFS__NODE_H_ */
