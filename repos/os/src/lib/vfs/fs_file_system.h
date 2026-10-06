/*
 * \brief  Adapter from Genode 'File_system' session to VFS
 * \author Norman Feske
 * \author Emery Hemingway
 * \author Christian Helmuth
 * \date   2011-02-17
 */

/*
 * Copyright (C) 2012-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__FS_FILE_SYSTEM_H_
#define _INCLUDE__VFS__FS_FILE_SYSTEM_H_

/* Genode includes */
#include <base/allocator_avl.h>
#include <base/id_space.h>
#include <file_system_session/connection.h>

namespace Vfs_fs {

	using namespace Genode;
	using namespace Genode::Vfs;

	class File_system;
}


class Vfs_fs::File_system : public Vfs::File_system, private Remote_io
{
	private:

		Vfs::Env &_env;

		Parent_fs &_parent_fs;

		Allocator_avl _fs_packet_alloc { &_env.alloc() };

		using Label_string = String<64>;
		Label_string _label;

		::File_system::Connection _fs;

		using Packet_descriptor = ::File_system::Packet_descriptor;

		bool _write_would_block = false;

		using Handle_space = Id_space<::File_system::Node>;

		Handle_space _handle_space { };
		Handle_space _watch_handle_space { };

		enum class Queued_state { IDLE, QUEUED, ACK };

		struct Handle_state
		{
			enum class Read_ready_state { IDLE, PENDING, READY };
			Read_ready_state read_ready_state = Read_ready_state::IDLE;

			Queued_state queued_read_state = Queued_state::IDLE;
			Queued_state queued_sync_state = Queued_state::IDLE;

			Packet_descriptor queued_read_packet { };
			Packet_descriptor queued_sync_packet { };
		};

		Remote_io::Peer _peer { _env.deferred_wakeups(), *this };

		/**
		 * Remote_io interface
		 */
		void wakeup_remote_peer() override { _fs.tx()->wakeup(); }

		/*
		 * Pass packet to server side
		 *
		 * The caller is expected to check 'ready_to_submit' before calling
		 * this function.
		 */
		void _submit_packet(Packet_descriptor const &packet)
		{
			/*
			 * The warning should never occur if the precondition above is
			 * satisfied.
			 */
			if (!_fs.tx()->ready_to_submit())
				warning("submit queue of file-system session unexpectedly full");
			else
				_fs.tx()->try_submit_packet(packet);

			_peer.schedule_wakeup();
		}

		/**
		 * Convert 'File_system::Node_type' to 'Dirent_type'
		 */
		static Dirent_type _dirent_type(::File_system::Node_type type)
		{
			using ::File_system::Node_type;

			switch (type) {
			case Node_type::DIRECTORY:          return Dirent_type::DIRECTORY;
			case Node_type::CONTINUOUS_FILE:    return Dirent_type::FILE;
			case Node_type::TRANSACTIONAL_FILE: return Dirent_type::DEVICE;
			case Node_type::SYMLINK:            return Dirent_type::SYMLINK;
			}
			return Dirent_type::FILE;
		}

		/**
		 * Convert 'File_system::Node_rwx' to 'Node_rwx'
		 */
		static Node_rwx _node_rwx(::File_system::Node_rwx rwx)
		{
			return { .readable   = rwx.readable,
			         .writeable  = rwx.writeable,
			         .executable = rwx.executable };
		}

		struct Open_fs_handle : Interface,
		                        private ::File_system::Node,
		                        private Handle_space::Element
		{
			friend Id_space<::File_system::Node>;

			Open_fs_handle(Handle_space &space, ::File_system::Node_handle node_handle)
			:
				Handle_space::Element(*this, space, node_handle)
			{ }

			::File_system::File_handle file_handle() const
			{
				return ::File_system::File_handle { id().value };
			}

			struct Handle_ack_result { bool release_packet; };

			virtual Handle_ack_result handle_ack(Packet_descriptor const &packet) = 0;
		};

		/**
		 * State of current mkdir operation, kept until mtime update is complete
		 */
		struct Mkdir_op : private Open_fs_handle
		{
			File_system &_fs;

			using Path = String<MAX_PATH_LEN>;
			Path const path;

			Mkdir_result const result;

			bool acked = false;

			Mkdir_op(File_system &fs, ::File_system::Dir_handle h, Path const &p,
			         Mkdir_result result)
			:
				Open_fs_handle(fs._handle_space, h),
				_fs(fs), path(p), result(result)
			{ }

			~Mkdir_op() { _fs._fs.close(file_handle()); }

			Handle_ack_result handle_ack(Packet_descriptor const &) override
			{
				acked = true;
				return { .release_packet = true };
			}
		};

		Constructible<Mkdir_op> _mkdir_op { };

		/**
		 * State of current symlink operation, kept until mtime update is complete
		 */
		struct Symlink_op : private Open_fs_handle
		{
			File_system &_fs;

			using Open_fs_handle::file_handle;

			using Path = String<MAX_PATH_LEN>;
			Path const path, target;

			Symlink_result const result;

			enum class State { PENDING, WRITTEN, MTIME_SUBMITTED, MTIME_ACKED };
			State state = State::PENDING;

			Symlink_op(File_system &fs, ::File_system::Symlink_handle h,
			           Path const &p, Path const &t, Symlink_result result)
			:
				Open_fs_handle(fs._handle_space, h),
				_fs(fs), path(p), target(t), result(result)
			{ }

			~Symlink_op() { _fs._fs.close(file_handle()); }

			Handle_ack_result handle_ack(Packet_descriptor const &packet) override
			{
				if (packet.operation() == Packet_descriptor::WRITE_TIMESTAMP)
					state = State::MTIME_ACKED;

				return { .release_packet = true };
			}
		};

		Constructible<Symlink_op> _symlink_op { };

		/**
		 * State of current follow operation
		 */
		struct Follow_op : private Open_fs_handle
		{
			File_system &_fs;

			using Open_fs_handle::file_handle;

			using Path = String<MAX_PATH_LEN>;
			Path const path;

			Path_elem const path_elem;
			Path target { }; /* result of read operation */

			enum class State { PENDING, READ_SUBMITTED };
			State state = State::PENDING;
			bool acked = false;

			Follow_op(File_system &fs, ::File_system::Symlink_handle h,
			          Path const &p, Path_elem e)
			:
				Open_fs_handle(fs._handle_space, h),
				_fs(fs), path(p), path_elem(e)
			{ }

			~Follow_op() { _fs._fs.close(file_handle()); }

			Handle_ack_result handle_ack(Packet_descriptor const &packet) override
			{
				::File_system::Session::Tx::Source &source = *_fs._fs.tx();
				target = { Cstring(source.packet_content(packet), packet.length()) };
				acked = true;
				return { .release_packet = true };
			}
		};

		Constructible<Follow_op> _follow_op { };

		template <typename RESULT, typename ERROR>
		static RESULT _read(File_system &fs, ::File_system::File_handle const fh,
		                    Queued_state      &queued_read_state,
		                    Packet_descriptor &queued_read_packet,
		                    At const at, Byte_range_ptr const &dst)
		{
			auto try_queue_read = [&]
			{
				if (queued_read_state != Queued_state::IDLE)
					return ERROR::RETRY;

				/* queue read into fs session */

				::File_system::Session::Tx::Source &source = *fs._fs.tx();

				/* if not ready to submit suggest retry */
				if (!source.ready_to_submit())
					return ERROR::RETRY;

				size_t const max_packet_size = source.bulk_buffer_size() / 2;
				size_t const clipped_count = min(max_packet_size, dst.num_bytes);

				Packet_descriptor p;
				try {
					p = source.alloc_packet((size_t)clipped_count);
				} catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					return ERROR::RETRY;
				}

				Packet_descriptor const packet(p, fh, Packet_descriptor::READ,
				                               (size_t)clipped_count, at.pos);

				queued_read_state = Queued_state::QUEUED;

				/* pass packet to server side */
				fs._submit_packet(packet);
				return ERROR::RETRY;
			};

			if (queued_read_state == Queued_state::IDLE)
				if (try_queue_read() == ERROR::DENIED)
					return ERROR::DENIED;

			if (queued_read_state != Queued_state::ACK)
				return ERROR::RETRY; /* Queued_state::QUEUED */

			::File_system::Session::Tx::Source &source = *fs._fs.tx();

			/* obtain result packet descriptor with updated status info */
			Packet_descriptor const packet = queued_read_packet;

			RESULT result = ERROR::DENIED;

			if (packet.succeeded()) {
				if (packet.position() == at.pos) {
					size_t const read_num_bytes = min(packet.length(), dst.num_bytes);
					memcpy(dst.start, source.packet_content(packet), (size_t)read_num_bytes);
					result = read_num_bytes;
				} else {
					result = ERROR::RETRY; /* drop response of discarded read */
				}
			}

			queued_read_state  = Queued_state::IDLE;
			queued_read_packet = Packet_descriptor();

			source.release_packet(packet);

			return result;
		}

		struct File_channel : Vfs::File_channel, private Open_fs_handle,
		                                         private Handle_state
		{
			using Handle_state::queued_read_state;
			using Handle_state::queued_read_packet;
			using Handle_state::queued_sync_packet;
			using Handle_state::queued_sync_state;
			using Handle_state::read_ready_state;

			using Open_fs_handle::file_handle;

			Allocator   &_alloc;
			File_system &_fs;

			File_channel(Allocator &alloc, Attr attr, File_system &fs,
			              Handle_space &space, ::File_system::Node_handle node_handle)
			:
				Vfs::File_channel(attr), Open_fs_handle(space, node_handle),
				_alloc(alloc), _fs(fs)
			{ }

			~File_channel() { _fs._fs.close(file_handle()); }

			/**
			 * Open_handle interface
			 */
			Handle_ack_result handle_ack(Packet_descriptor const &packet) override
			{
				if (!packet.succeeded())
					error("packet operation=", (int)packet.operation(), " failed");

				switch (packet.operation()) {
				case Packet_descriptor::READ_READY:
					read_ready_state = Handle_state::Read_ready_state::READY;
					read_ready_response();
					return { };

				case Packet_descriptor::READ:
					queued_read_packet = packet;
					queued_read_state  = Queued_state::ACK;
					return { };

				case Packet_descriptor::SYNC:
					queued_sync_packet = packet;
					queued_sync_state  = Queued_state::ACK;
					return { };

				case Packet_descriptor::CONTENT_CHANGED:
					return { };

				case Packet_descriptor::WRITE:
				case Packet_descriptor::WRITE_TIMESTAMP:
					return { .release_packet = true };
				}
				return { };
			};

			virtual Write_result write(At const at, Const_byte_range_ptr const &src) override
			{
				/* reclaim as much space in the packet stream as possible */
				_fs._handle_ack();

				::File_system::Session::Tx::Source &source = *_fs._fs.tx();

				size_t const max_packet_size = source.bulk_buffer_size() / 2;
				size_t const count = min(max_packet_size, src.num_bytes);

				if (!source.ready_to_submit()) {
					_fs._write_would_block = true;
					return Write_error::RETRY;
				}

				try {
					Packet_descriptor packet_in(source.alloc_packet(count),
					                            file_handle(),
					                            Packet_descriptor::WRITE,
					                            count,
					                            at.pos);

					memcpy(source.packet_content(packet_in), src.start, count);

					_fs._submit_packet(packet_in);
				}
				catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					_fs._write_would_block = true;
					return Write_error::RETRY;
				}
				catch (...) {
					error("unhandled exception");
					return Write_error::DENIED;
				}
				return count;
			}

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (queued_read_state == Queued_state::IDLE)
					read_ready_state  = Handle_state::Read_ready_state::IDLE;

				return _read<Read_result, Read_error>(_fs, file_handle(),
				                                      queued_read_state,
				                                      queued_read_packet, at, dst);
			}

			bool read_ready() const override
			{
				return read_ready_state == Handle_state::Read_ready_state::READY;
			}

			bool write_ready() const override
			{
				return !_fs._write_would_block;
			}

			Sync_result sync() override
			{
				if (queued_sync_state == Queued_state::IDLE) {

					::File_system::Session::Tx::Source &source = *_fs._fs.tx();

					/* if not ready to submit suggest retry */
					if (!source.ready_to_submit()) return Sync_result::RETRY;

					::File_system::Packet_descriptor p;
					try {
						p = source.alloc_packet(0);
					} catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
						return Sync_result::RETRY;
					}

					::File_system::Packet_descriptor const
						packet(p, file_handle(),
						       ::File_system::Packet_descriptor::SYNC, 0, 0);

					queued_sync_state = Queued_state::QUEUED;

					/* pass packet to server side */
					_fs._submit_packet(packet);
				}

				if (queued_sync_state == Queued_state::ACK) {

					/* obtain result packet descriptor */
					::File_system::Packet_descriptor const
						packet = queued_sync_packet;

					::File_system::Session::Tx::Source &source = *_fs._fs.tx();

					bool const ok = packet.succeeded();

					queued_sync_state  = Queued_state::IDLE;
					queued_sync_packet = ::File_system::Packet_descriptor();

					source.release_packet(packet);

					if (ok)
						return Sync_result::OK;
					else
						error("vfs_fs: sync failed");
				}

				return Sync_result::RETRY;
			}

			Update_mtime_result update_mtime(Timestamp time) override
			{
				::File_system::Session::Tx::Source &source = *_fs._fs.tx();
				using ::File_system::Packet_descriptor;

				if (!source.ready_to_submit())
					return Update_mtime_result::RETRY;

				try {
					Packet_descriptor p(source.alloc_packet(0),
					                    file_handle(),
					                    Packet_descriptor::WRITE_TIMESTAMP,
					                    ::File_system::Timestamp {
					                       .ms_since_1970 = time.ms_since_1970 });

					_fs._submit_packet(p);
				}
				catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					return Update_mtime_result::RETRY; }

				return Update_mtime_result::OK;
			}

			void notify_read_ready() override
			{
				if (read_ready_state != Handle_state::Read_ready_state::IDLE)
					return;

				::File_system::Session::Tx::Source &source = *_fs._fs.tx();

				/* if not ready to submit suggest retry */
				if (!source.ready_to_submit()) return;

				using ::File_system::Packet_descriptor;

				Packet_descriptor packet(Packet_descriptor(),
				                         file_handle(),
				                         Packet_descriptor::READ_READY,
				                         0, 0);

				read_ready_state = Handle_state::Read_ready_state::PENDING;

				_fs._submit_packet(packet);

				/*
				 * When the packet is acknowledged the application is notified via
				 * Response_handler::handle_response().
				 */
			}

			Resize_result resize(file_size len) override
			{
				try {
					_fs._fs.truncate(file_handle(), len);
				}
				catch (::File_system::Invalid_handle)    { return Resize_result::DENIED; }
				catch (::File_system::Permission_denied) { return Resize_result::DENIED; }
				catch (::File_system::No_space)          { return Resize_result::DENIED; }
				catch (::File_system::Unavailable)       { return Resize_result::DENIED; }

				return Resize_result::OK;
			}

			void destruct() override { destroy(_alloc, this); }
		};

		struct Fs_dir_channel : Vfs::Dir_channel, private Open_fs_handle
		{
			File_system &_fs;
			Allocator   &_alloc;

			Queued_state      _queued_read_state { };
			Packet_descriptor _queued_read_packet { };

			Fs_dir_channel(File_system &fs, Allocator &alloc, Handle_space &space,
			               ::File_system::Node_handle node_handle)
			:
				Open_fs_handle(space, node_handle), _fs(fs), _alloc(alloc)
			{ }

			~Fs_dir_channel() { _fs._fs.close(file_handle()); }

			void destruct() override {destroy(_alloc, this); }

			enum { DIRENT_SIZE = sizeof(::File_system::Directory_entry) };

			Read_dir_result read_dir(At const at, Dirent_range_ptr const &dst) override
			{
				if (dst.num_entries < 1)
					return Read_dir_error::DENIED;

				using ::File_system::Directory_entry;

				Directory_entry entry { };
				Byte_range_ptr entry_bytes((char *)(&entry), DIRENT_SIZE);

				File_channel::Read_result const read_result =
					_read<File_channel::Read_result, File_channel::Read_error>(
						_fs, file_handle(),
						_queued_read_state, _queued_read_packet,
						{ at.pos*DIRENT_SIZE }, entry_bytes);

				return read_result.convert<Read_dir_result>(
					[&] (size_t num_bytes) -> Read_dir_result {
						if (num_bytes < DIRENT_SIZE)
							return Read_dir_end();

						entry.sanitize();

						dst.start[0] = Dirent {
							.type = _dirent_type(entry.type),
							.rwx  = _node_rwx(entry.rwx),
							.name = { entry.name.buf }
						};
						return 1;
					},
					[&] (File_channel::Read_error e) {
						switch (e) {
						case File_channel::Read_error::RETRY:  return Read_dir_error::RETRY;
						case File_channel::Read_error::DENIED: break;
						}
						return Read_dir_error::DENIED;
					});
			}

			/**
			 * Open_handle interface
			 */
			Handle_ack_result handle_ack(Packet_descriptor const &packet) override
			{
				if (!packet.succeeded())
					error("vfs_fs: dir packet operation=", (int)packet.operation(), " failed");

				if (packet.operation() != Packet_descriptor::READ)
					error("vfs_fs: ACK for unexpected dir operation ", (int)packet.operation());

				_queued_read_packet = packet;
				_queued_read_state  = Queued_state::ACK;
				return { };
			};
		};

		/**
		 * Helper for managing the lifetime of temporary open node handles
		 */
		struct Fs_handle_guard : Noncopyable
		{
			File_system &_fs;

			::File_system::Node_handle _handle;

			Fs_handle_guard(File_system &fs, ::File_system::Node_handle handle)
			:
				_fs(fs), _handle(handle)
			{ }

			~Fs_handle_guard() { _fs._fs.close(_handle); }
		};

		using Watched_path = String<MAX_PATH_LEN>;

		struct Fs_watch_handle : private ::File_system::Node,
		                         private Handle_space::Element
		{
			friend Id_space<::File_system::Node>;

			Watched_path const path;

			::File_system::Watch_handle const fs_handle;

			Fs_watch_handle(Watched_path const &path, Handle_space &space,
			                ::File_system::Watch_handle handle)
			:
				Handle_space::Element(*this, space, handle),
				path(path), fs_handle(handle)
			{ }
		};

		void _handle_ack()
		{
			::File_system::Session::Tx::Source &source = *_fs.tx();
			using ::File_system::Packet_descriptor;

			bool any_ack_handled = false;

			while (source.ack_avail()) {

				Packet_descriptor const packet = source.try_get_acked_packet();
				_write_would_block = false;
				_peer.schedule_wakeup();

				Handle_space::Id const id(packet.handle());

				if (packet.operation() == Packet_descriptor::CONTENT_CHANGED)
					_watch_handle_space.apply<Fs_watch_handle>(id,
						[&] (Fs_watch_handle &handle) {
							handle.path.with_span([&] (Span const &s) {
								_parent_fs.notify_watchers(s); });
						},
						[&] {
							warning("ack for unknown watch handle ", id);
						});
				else
					_handle_space.apply<Open_fs_handle>(id,
						[&] (Open_fs_handle &handle) {
							if (handle.handle_ack(packet).release_packet) {
								source.release_packet(packet);
								any_ack_handled = true;
							}
						},
						[&] {
							warning("ack for unknown File_system handle ", id,
							        " op=", (int)packet.operation());
						});

				if (packet.succeeded())
					any_ack_handled = true;
			}

			if (any_ack_handled)
				_env.user().wakeup_vfs_user();
		}

		Io_signal_handler<File_system> _signal_handler {
			_env.env().ep(), *this, &File_system::_handle_ack };

		static size_t buffer_size(Node const &config)
		{
			Number_of_bytes fs_default { ::File_system::DEFAULT_TX_BUF_SIZE };
			return config.attribute_value("buffer_size", fs_default);
		}

	public:

		File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &config)
		:
			Vfs::File_system(Ident::from_node(config)),
			_env(env), _parent_fs(parent_fs),
			_label(config.attribute_value("label", Label_string("/"))),
			_fs(_env.env(), _fs_packet_alloc,
			    _label,
			    config.attribute_value("writeable", true),
			    buffer_size(config))
		{
			if (config.has_attribute("root")) {
				warning("vfs: 'fs' node uses deprecated 'root' attribute.");
				warning("      Append the root dir to the label instead.");
			}

			_fs.sigh(_signal_handler);
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			::File_system::Status status;

			try {
				::File_system::Node_handle node = _fs.node(path);
				Fs_handle_guard node_guard(*this, node);
				status = _fs.status(node);
			}
			catch (Out_of_ram)  {
				error("out-of-ram during stat");
				return Stat_result::DENIED;
			}
			catch (Out_of_caps) {
				error("out-of-caps during stat");
				return Stat_result::DENIED;
			}
			catch (...) { return Stat_result::DENIED; }

			out = Stat();

			out.size = status.size;
			out.type = _dirent_type(status.type);
			out.rwx  = _node_rwx(status.rwx);
			out.modification_time = {
				.ms_since_1970 = status.modification_time.ms_since_1970 };

			return Stat_result::OK;
		}

		Unlink_result unlink(char const *path) override
		{
			Absolute_path dir_path(path);
			dir_path.strip_last_element();

			Absolute_path file_name(path);
			file_name.keep_only_last_element();

			try {
				::File_system::Dir_handle dir = _fs.dir(dir_path.base(), false);
				Fs_handle_guard dir_guard(*this, dir);

				_fs.unlink(dir, file_name.base() + 1);
				return Unlink_result::OK;
			}
			catch (::File_system::Invalid_handle)    { }
			catch (::File_system::Invalid_name)      { }
			catch (::File_system::Lookup_failed)     { }
			catch (::File_system::Not_empty)         { }
			catch (::File_system::Permission_denied) { }
			catch (::File_system::Unavailable)       { }

			return Unlink_result::DENIED;
		}

		Rename_result rename(char const *from_path, char const *to_path) override
		{
			if ((strcmp(from_path, to_path) == 0) && dir_entry_exists(from_path))
				return Rename_result::OK;

			Absolute_path from_dir_path(from_path);
			from_dir_path.strip_last_element();

			Absolute_path from_file_name(from_path);
			from_file_name.keep_only_last_element();

			Absolute_path to_dir_path(to_path);
			to_dir_path.strip_last_element();

			Absolute_path to_file_name(to_path);
			to_file_name.keep_only_last_element();

			try {
				::File_system::Dir_handle from_dir =
					_fs.dir(from_dir_path.base(), false);

				Fs_handle_guard from_dir_guard(*this, from_dir);

				::File_system::Dir_handle to_dir = _fs.dir(to_dir_path.base(),
				                                           false);
				Fs_handle_guard to_dir_guard(*this, to_dir);

				_fs.move(from_dir, from_file_name.base() + 1,
				         to_dir,   to_file_name.base() + 1);

				return Rename_result::OK;
			}
			catch (...) { }
			return Rename_result::DENIED;
		}

		Mkdir_result mkdir(char const *path, Timestamp ts) override
		{
			Absolute_path dir_path(path);

			using ::File_system::Packet_descriptor;
			using Tx = ::File_system::Session::Tx;

			/* cancel mkdir operation if paths mismatch */
			if (_mkdir_op.constructed() && _mkdir_op->path != path)
				_mkdir_op.destruct();

			if (!_mkdir_op.constructed()) {
				Tx::Source &source = *_fs.tx();

				/* check precondition for mtime update */
				if (!source.ready_to_submit())
					return Mkdir_result::RETRY;

				bool already_exists = false;
				Mkdir_result result = Mkdir_result::DENIED;
				::File_system::Dir_handle dir { ~0u };

				auto try_open_dir = [&] (bool create)
				{
					try {
						dir = _fs.dir(dir_path.base(), create);
						result  = create ? Mkdir_result::CREATED
						                 : Mkdir_result::UPDATED;
					}
					catch (::File_system::Lookup_failed)       { }
					catch (::File_system::Name_too_long)       { }
					catch (::File_system::Node_already_exists) { already_exists = true; }
					catch (::File_system::No_space)            { }
					catch (::File_system::Permission_denied)   { }
					catch (Out_of_ram)                         { }
					catch (Out_of_caps)                        { }
				};

				try_open_dir(true);
				if (already_exists)
					try_open_dir(false);

				bool const ok = (result == Mkdir_result::CREATED)
				             || (result == Mkdir_result::UPDATED);
				if (!ok)
					return result;

				/* update mtime */
				try {
					Packet_descriptor p(source.alloc_packet(0), dir,
					                    Packet_descriptor::WRITE_TIMESTAMP,
					                    ::File_system::Timestamp {
					                       .ms_since_1970 = ts.ms_since_1970 });
					_submit_packet(p);
				}
				catch (Tx::Source::Packet_alloc_failed) { result = Mkdir_result::RETRY; }

				_mkdir_op.construct(*this, dir, path, result);
			}

			/* '_mkdir_op' cannot be unconstructed at this point */

			if (!_mkdir_op->acked)
				return Mkdir_result::RETRY;

			Mkdir_result const result = _mkdir_op->result;
			_mkdir_op.destruct();
			return result;
		}

		Symlink_result symlink(char const *path, char const *target, Timestamp ts) override
		{
			/* cancel incomplete symlink operation if paths mismatch */
			if (_symlink_op.constructed())
				if (_symlink_op->path != path || _symlink_op->target != target)
					_symlink_op.destruct();

			Absolute_path abs_path(path);
			abs_path.strip_last_element();

			Absolute_path symlink_name(path);
			symlink_name.keep_only_last_element();

			if (!_symlink_op.constructed()) {

				try {
					::File_system::Dir_handle dir_handle = _fs.dir(abs_path.base(), false);

					Fs_handle_guard from_dir_guard(*this, dir_handle);

					bool already_exists = false;
					Symlink_result result = Symlink_result::DENIED;
					::File_system::Symlink_handle symlink { ~0u };

					auto try_open_symlink = [&] (bool create)
					{
						try {
							auto const &name_wo_slash = symlink_name.string() + 1;
							symlink = _fs.symlink(dir_handle, name_wo_slash, create);
							result  = create ? Symlink_result::CREATED
							                 : Symlink_result::UPDATED;
						}
						catch (::File_system::Invalid_handle)      { }
						catch (::File_system::Invalid_name)        { }
						catch (::File_system::Lookup_failed)       { }
						catch (::File_system::Node_already_exists) { already_exists = true; }
						catch (::File_system::No_space)            { }
						catch (::File_system::Permission_denied)   { }
						catch (::File_system::Unavailable)         { }
						catch (Out_of_ram)                         { }
						catch (Out_of_caps)                        { }
					};

					try_open_symlink(true);
					if (already_exists)
						try_open_symlink(false);

					bool const ok = (result == Symlink_result::CREATED)
					             || (result == Symlink_result::UPDATED);
					if (!ok)
						return result;

					_symlink_op.construct(*this, symlink, path, target, result);

				} catch (...) { return Symlink_result::DENIED; }
			}

			/* _symlink_op is constructed at this point */

			using ::File_system::Packet_descriptor;
			using Tx = ::File_system::Session::Tx;

			Tx::Source &source = *_fs.tx();

			if (_symlink_op->state == Symlink_op::State::PENDING) {

				/* check precondition for write operation */
				if (!source.ready_to_submit())
					return Symlink_result::RETRY;

				try {
					size_t const n = strlen(target);
					Packet_descriptor packet_in(source.alloc_packet(n),
					                            _symlink_op->file_handle(),
					                            Packet_descriptor::WRITE,
					                            n, 0);

					memcpy(source.packet_content(packet_in), target, n);

					_submit_packet(packet_in);
					_symlink_op->state = Symlink_op::State::WRITTEN;
				}
				catch (Tx::Source::Packet_alloc_failed) { return Symlink_result::RETRY; }
			}

			if (_symlink_op->state == Symlink_op::State::WRITTEN) {

				/* check precondition for mtime update operation */
				if (!source.ready_to_submit())
					return Symlink_result::RETRY;

				/* update mtime */
				try {
					Packet_descriptor p(source.alloc_packet(0), _symlink_op->file_handle(),
					                    Packet_descriptor::WRITE_TIMESTAMP,
					                    ::File_system::Timestamp {
					                       .ms_since_1970 = ts.ms_since_1970 });
					_submit_packet(p);

					_symlink_op->state = Symlink_op::State::MTIME_SUBMITTED;
				}
				catch (Tx::Source::Packet_alloc_failed) { return Symlink_result::RETRY; }
			}

			if (_symlink_op->state != Symlink_op::State::MTIME_ACKED)
				return Symlink_result::RETRY;

			Symlink_result const result = _symlink_op->result;
			_symlink_op.destruct();
			return result;
		}

		Follow_result follow(char const *path, Byte_range_ptr const &dst) override
		{
			if (!dst.num_bytes) /* no space for null-termination */
				return Follow_error::DENIED;

			/* cancel incomplete follow operation if paths mismatch */
			if (_follow_op.constructed() && (_follow_op->path != path))
				_follow_op.destruct();

			if (!_follow_op.constructed()) {

				Follow_result const result = follow_path(Span::from_cstring(path),
					[&] (auto const &partial_path) {
						return _symlink(partial_path.string());
					},
					[&] (Path_elem const elem, auto const &partial_path) -> Follow_result {

						Absolute_path abs_path(partial_path);
						abs_path.strip_last_element();

						Absolute_path symlink_name(partial_path);
						symlink_name.keep_only_last_element();

						try {
							::File_system::Dir_handle dir_handle = _fs.dir(abs_path.base(), false);

							Fs_handle_guard from_dir_guard(*this, dir_handle);

							auto const &name_wo_slash = symlink_name.string() + 1;
							::File_system::Symlink_handle symlink =
								_fs.symlink(dir_handle, name_wo_slash, false);

							_follow_op.construct(*this, symlink, path, elem);

							return elem;

						} catch (...) { return Follow_error::DENIED; }
					});

				if (result.failed())
					return result;
			}

			/* _follow_op is constructed at this point */

			if (_follow_op->state == Follow_op::State::PENDING) {

				using ::File_system::Packet_descriptor;
				using Tx = ::File_system::Session::Tx;

				Tx::Source &source = *_fs.tx();

				/* check precondition for read operation */
				if (!source.ready_to_submit())
					return Follow_error::RETRY;

				try {
					Packet_descriptor packet_in(source.alloc_packet(dst.num_bytes),
					                            _follow_op->file_handle(),
					                            Packet_descriptor::READ,
					                            dst.num_bytes, 0);

					_submit_packet(packet_in);
					_follow_op->state = Follow_op::State::READ_SUBMITTED;
				}
				catch (Tx::Source::Packet_alloc_failed) { return Follow_error::RETRY; }
			}

			if (!_follow_op->acked)
				return Follow_error::RETRY;

			_follow_op->target.with_span([&] (Span const &src) {
				size_t n = min(dst.num_bytes,
				               src.num_bytes + 1 /* null termination */);
				copy_cstring(dst.start, src.start, n);
			});

			Path_elem const result = _follow_op->path_elem;
			_follow_op.destruct();
			return result;
		}

		unsigned num_dirent(char const *path) override
		{
			if (strcmp(path, "") == 0)
				path = "/";

			try {
				::File_system::Dir_handle dir = _fs.dir(path, false);
				Fs_handle_guard node_guard(*this, dir);

				return _fs.num_entries(dir);
			}
			catch (...) { }
			return 0;
		}

		bool directory(char const *path) override
		{
			try {
				::File_system::Node_handle node = _fs.node(path);
				Fs_handle_guard node_guard(*this, node);

				::File_system::Status status = _fs.status(node);

				return status.directory();
			}
			catch (...) { }
			return false;
		}

		bool _symlink(char const *path)
		{
			try {
				::File_system::Node_handle node = _fs.node(path);
				Fs_handle_guard node_guard(*this, node);

				::File_system::Status status = _fs.status(node);

				return status.symlink();
			}
			catch (...) { }
			return false;
		}

		bool dir_entry_exists(char const *path) override
		{
			/* check if node at path exists within file system */
			try {
				::File_system::Node_handle node = _fs.node(path);
				_fs.close(node);
			}
			catch (...) { return false; }

			return true;
		}

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
		{
			Absolute_path dir_path(path);
			dir_path.strip_last_element();

			Absolute_path file_name(path);
			file_name.keep_only_last_element();

			::File_system::Mode const mode = attr.writeable
			                               ? ::File_system::READ_WRITE
			                               : ::File_system::READ_ONLY;
			try {
				::File_system::Dir_handle dir = _fs.dir(dir_path.base(), false);
				Fs_handle_guard dir_guard(*this, dir);

				::File_system::File_handle file = _fs.file(dir,
				                                           file_name.base() + 1,
				                                           mode, attr.create);
				return *new (alloc)
					File_channel(alloc, { .writeable = attr.writeable }, *this, _handle_space, file);
			}
			catch (::File_system::Lookup_failed)       { }
			catch (::File_system::Permission_denied)   { }
			catch (::File_system::Invalid_handle)      { }
			catch (::File_system::Node_already_exists) { }
			catch (::File_system::Invalid_name)        { }
			catch (::File_system::Name_too_long)       { }
			catch (::File_system::No_space)            { }
			catch (::File_system::Unavailable)         { }
			catch (Out_of_ram)  { return Open_error::OUT_OF_RAM;  }
			catch (Out_of_caps) { return Open_error::OUT_OF_CAPS; }

			return Open_error::DENIED;
		}

		Opendir_result opendir(char const *path, Allocator &alloc) override
		{
			Absolute_path dir_path(path);

			Opendir_error error = Opendir_error::DENIED;

			::File_system::Dir_handle dir { ~0u };
			try {
				dir = _fs.dir(dir_path.base(), false);
				return *new (alloc) Fs_dir_channel(*this, alloc, _handle_space, dir);
			}
			catch (::File_system::Lookup_failed)       { }
			catch (::File_system::Name_too_long)       { }
			catch (::File_system::Node_already_exists) { }
			catch (::File_system::No_space)            { }
			catch (::File_system::Permission_denied)   { }
			catch (Out_of_ram)                         { error = Opendir_error::OUT_OF_RAM; }
			catch (Out_of_caps)                        { error = Opendir_error::OUT_OF_CAPS; }

			if (dir.value != ~0u)
				_fs.close(dir);

			return error;
		}

		Watch_result watch(char const *path) override
		{
			using namespace ::File_system;

			::File_system::Watch_handle fs_handle { -1UL };

			try { fs_handle = _fs.watch(path); }
			catch (Out_of_ram)  { return Alloc_error::OUT_OF_RAM; }
			catch (Out_of_caps) { return Alloc_error::OUT_OF_CAPS; }
			catch (...)         { return Alloc_error::DENIED; }

			Watch_result result = Alloc_error::DENIED;
			try {
				new (_env.alloc())
					Fs_watch_handle(path, _watch_handle_space, fs_handle);
				return Ok();
			}
			catch (Out_of_ram)  { result = Alloc_error::OUT_OF_RAM; }
			catch (Out_of_caps) { result = Alloc_error::OUT_OF_CAPS; }
			catch (...)         { result = Alloc_error::DENIED; }
			_fs.close(fs_handle);
			return result;
		}

		void unwatch(char const *path) override
		{
			/* look up watch handle for the given path */
			Fs_watch_handle const *ptr = nullptr;
			_watch_handle_space.for_each<Fs_watch_handle>(
				[&] (Fs_watch_handle const &handle) {
					if (handle.path == path)
						ptr = &handle; });
			if (ptr) {
				_fs.close(ptr->fs_handle);
				destroy(_env.alloc(), const_cast<Fs_watch_handle *>(ptr));
			}
		};

		static constexpr auto BUILTIN_FS_TYPE = "fs";
};

#endif /* _INCLUDE__VFS__FS_FILE_SYSTEM_H_ */
