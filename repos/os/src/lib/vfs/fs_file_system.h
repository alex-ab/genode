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

		bool _write_would_block = false;

		using Handle_space = Id_space<::File_system::Node>;

		Handle_space _handle_space { };
		Handle_space _watch_handle_space { };

		struct Handle_state
		{
			enum class Read_ready_state { IDLE, PENDING, READY };
			Read_ready_state read_ready_state = Read_ready_state::IDLE;

			enum class Queued_state { IDLE, QUEUED, ACK };
			Queued_state queued_read_state = Queued_state::IDLE;
			Queued_state queued_sync_state = Queued_state::IDLE;

			::File_system::Packet_descriptor queued_read_packet { };
			::File_system::Packet_descriptor queued_sync_packet { };
		};

		struct Fs_vfs_handle;
		using Fs_vfs_handle_queue = Fifo<Fs_vfs_handle>;

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
		void _submit_packet(::File_system::Packet_descriptor const &packet)
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
			case Node_type::CONTINUOUS_FILE:    return Dirent_type::CONTINUOUS_FILE;
			case Node_type::TRANSACTIONAL_FILE: return Dirent_type::TRANSACTIONAL_FILE;
			case Node_type::SYMLINK:            return Dirent_type::SYMLINK;
			}
			return Dirent_type::END;
		}

		/**
		 * Convert 'File_system::Node_type' to 'Node_type'
		 */
		static Node_type _node_type(::File_system::Node_type type)
		{
			using Type = ::File_system::Node_type;

			switch (type) {
			case Type::DIRECTORY:          return Node_type::DIRECTORY;
			case Type::CONTINUOUS_FILE:    return Node_type::CONTINUOUS_FILE;
			case Type::TRANSACTIONAL_FILE: return Node_type::TRANSACTIONAL_FILE;
			case Type::SYMLINK:            return Node_type::SYMLINK;
			}

			error("invalid File_system::Node_type");
			return Node_type::CONTINUOUS_FILE;
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

		struct Fs_vfs_handle : Vfs_handle,
		                       private ::File_system::Node,
		                       private Handle_space::Element,
		                       private Handle_state
		{
			friend Id_space<::File_system::Node>;
			friend Fs_vfs_handle_queue;

			using Handle_state::queued_read_state;
			using Handle_state::queued_read_packet;
			using Handle_state::queued_sync_packet;
			using Handle_state::queued_sync_state;
			using Handle_state::read_ready_state;

			File_system &_fs;

			Fs_vfs_handle(File_system &fs, Allocator &alloc,
			              int status_flags, Handle_space &space,
			              ::File_system::Node_handle node_handle)
			:
				Vfs_handle(fs, alloc, status_flags),
				Handle_space::Element(*this, space, node_handle),
				_fs(fs)
			{ }

			::File_system::File_handle file_handle() const
			{
				return ::File_system::File_handle { id().value };
			}

			Write_result _write(file_size const seek_offset,
			                    Const_byte_range_ptr const &src, size_t &out_count)
			{
				/* reclaim as much space in the packet stream as possible */
				_fs._handle_ack();

				::File_system::Session::Tx::Source &source = *_fs._fs.tx();
				using ::File_system::Packet_descriptor;

				size_t const max_packet_size = source.bulk_buffer_size() / 2;
				size_t const count = min(max_packet_size, src.num_bytes);

				if (!source.ready_to_submit()) {
					_fs._write_would_block = true;
					return Write_result::WRITE_ERR_WOULD_BLOCK;
				}

				try {
					Packet_descriptor packet_in(source.alloc_packet(count),
					                            file_handle(),
					                            Packet_descriptor::WRITE,
					                            count,
					                            seek_offset);

					memcpy(source.packet_content(packet_in), src.start, count);

					_fs._submit_packet(packet_in);
				}
				catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					_fs._write_would_block = true;
					return Write_result::WRITE_ERR_WOULD_BLOCK;
				}
				catch (...) {
					error("unhandled exception");
					return Write_result::WRITE_ERR_IO;
				}
				out_count = count;
				return Write_result::WRITE_OK;
			}

			virtual Write_result write(Const_byte_range_ptr const &src,
			                           size_t &out_count) override
			{
				return _write(seek(), src, out_count);
			}

			Read_result _try_queue_read(Byte_range_ptr const &dst,
			                            file_size const seek_offset)
			{
				if (queued_read_state != Handle_state::Queued_state::IDLE)
					return Read_error::RETRY;

				/* queue read into fs session */

				::File_system::Session::Tx::Source &source = *_fs._fs.tx();

				/* if not ready to submit suggest retry */
				if (!source.ready_to_submit())
					return Read_error::RETRY;

				size_t const max_packet_size = source.bulk_buffer_size() / 2;
				size_t const clipped_count = min(max_packet_size, dst.num_bytes);

				::File_system::Packet_descriptor p;
				try {
					p = source.alloc_packet((size_t)clipped_count);
				} catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					return Read_error::RETRY;
				}

				::File_system::Packet_descriptor const
					packet(p, file_handle(),
					       ::File_system::Packet_descriptor::READ,
					       (size_t)clipped_count, seek_offset);

				read_ready_state  = Handle_state::Read_ready_state::IDLE;
				queued_read_state = Handle_state::Queued_state::QUEUED;

				/* pass packet to server side */
				_fs._submit_packet(packet);
				return Read_error::RETRY;
			}

			Read_result read(Byte_range_ptr const &dst) override
			{
				if (queued_read_state == Handle_state::Queued_state::IDLE)
					if (_try_queue_read(dst, seek()) == Read_error::DENIED)
						return Read_error::DENIED;

				if (queued_read_state != Handle_state::Queued_state::ACK)
					return Read_error::RETRY; /* Queued_state::QUEUED */

				::File_system::Session::Tx::Source &source = *_fs._fs.tx();

				/* obtain result packet descriptor with updated status info */
				::File_system::Packet_descriptor const packet = queued_read_packet;

				Read_result result = Read_error::DENIED;

				if (packet.succeeded()) {
					if (packet.position() == seek()) {
						size_t const read_num_bytes = min(packet.length(), dst.num_bytes);
						memcpy(dst.start, source.packet_content(packet), (size_t)read_num_bytes);
						result = read_num_bytes;
					} else {
						result = Read_error::RETRY; /* drop response of discarded read */
					}
				}

				queued_read_state  = Handle_state::Queued_state::IDLE;
				queued_read_packet = ::File_system::Packet_descriptor();

				source.release_packet(packet);

				return result;
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
				if (queued_sync_state == Handle_state::Queued_state::IDLE) {

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

					queued_sync_state = Handle_state::Queued_state::QUEUED;

					/* pass packet to server side */
					_fs._submit_packet(packet);
				}

				if (queued_sync_state == Handle_state::Queued_state::ACK) {

					/* obtain result packet descriptor */
					::File_system::Packet_descriptor const
						packet = queued_sync_packet;

					::File_system::Session::Tx::Source &source = *_fs._fs.tx();

					bool const ok = packet.succeeded();

					queued_sync_state  = Handle_state::Queued_state::IDLE;
					queued_sync_packet = ::File_system::Packet_descriptor();

					source.release_packet(packet);

					if (ok)
						return Sync_result::OK;
					else
						error("vfs_fs: sync failed");
				}

				return Sync_result::RETRY;
			}

			bool update_modification_timestamp(Timestamp time) override
			{
				::File_system::Session::Tx::Source &source = *_fs._fs.tx();
				using ::File_system::Packet_descriptor;

				if (!source.ready_to_submit()) {
					return false;
				}

				try {
					Packet_descriptor p(source.alloc_packet(0),
					                    file_handle(),
					                    Packet_descriptor::WRITE_TIMESTAMP,
					                    ::File_system::Timestamp {
					                       .ms_since_1970 = time.ms_since_1970 });

					_fs._submit_packet(p);
				} catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					return false;
				}

				return true;
			}

			bool notify_read_ready() override
			{
				if (read_ready_state != Handle_state::Read_ready_state::IDLE)
					return true;

				::File_system::Session::Tx::Source &source = *_fs._fs.tx();

				/* if not ready to submit suggest retry */
				if (!source.ready_to_submit()) return false;

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
				return true;
			}

			Ftruncate_result ftruncate(file_size len) override
			{
				try {
					_fs._fs.truncate(file_handle(), len);
				}
				catch (::File_system::Invalid_handle)    { return FTRUNCATE_ERR_NO_PERM; }
				catch (::File_system::Permission_denied) { return FTRUNCATE_ERR_NO_PERM; }
				catch (::File_system::No_space)          { return FTRUNCATE_ERR_NO_SPACE; }
				catch (::File_system::Unavailable)       { return FTRUNCATE_ERR_NO_PERM; }

				return FTRUNCATE_OK;
			}
		};

		struct Fs_vfs_file_handle : Fs_vfs_handle
		{
			using Fs_vfs_handle::Fs_vfs_handle;
		};

		struct Fs_vfs_dir_handle : Fs_vfs_handle
		{
			enum { DIRENT_SIZE = sizeof(::File_system::Directory_entry) };

			using Fs_vfs_handle::Fs_vfs_handle;

			Read_result read(Byte_range_ptr const &dst) override
			{
				if (dst.num_bytes < sizeof(Dirent))
					return Read_error::DENIED;

				file_size const seek_offset = seek();
				if (seek_offset % sizeof(Dirent)) /* must be aligned to 'Dirent' */
					return Read_error::DENIED;

				using ::File_system::Directory_entry;

				Directory_entry entry { };

				Read_result const read_result =
					Fs_vfs_handle::read(Byte_range_ptr((char *)(&entry), DIRENT_SIZE));

				return read_result.convert<Read_result>([&] (size_t num_bytes) {

					entry.sanitize();

					Dirent &dirent = *(Dirent*)dst.start;

					if (num_bytes < DIRENT_SIZE) {

						/* no entry found for the given index, or error */
						dirent = Dirent {
							.type = Dirent_type::END,
							.rwx  = { },
							.name = { }
						};
					} else {
						dirent = Dirent {
							.type = _dirent_type(entry.type),
							.rwx  = _node_rwx(entry.rwx),
							.name = { entry.name.buf }
						};
					}
					return sizeof(Dirent);
				},
				[&] (Read_error e) { return e; });
			}
		};

		struct Fs_vfs_symlink_handle : Fs_vfs_handle
		{
			using Fs_vfs_handle::Fs_vfs_handle;
		};

		/**
		 * Helper for managing the lifetime of temporary open node handles
		 */
		struct Fs_handle_guard : Fs_vfs_handle
		{
			Fs_handle_guard(File_system &fs,
			                ::File_system::Node_handle fs_handle,
			                Handle_space &space)
			:
				Fs_vfs_handle(fs, *(Allocator*)nullptr, 0, space, fs_handle)
			{ }

			~Fs_handle_guard()
			{
				_fs._fs.close(file_handle());
			}
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

				auto handle_fn = [&] (Fs_vfs_handle &handle)
				{
					if (!packet.succeeded())
						error("packet operation=", (int)packet.operation(), " failed");

					switch (packet.operation()) {
					case Packet_descriptor::READ_READY:
						handle.read_ready_state = Handle_state::Read_ready_state::READY;
						handle.read_ready_response();
						break;

					case Packet_descriptor::READ:
						handle.queued_read_packet = packet;
						handle.queued_read_state  = Handle_state::Queued_state::ACK;
						break;

					case Packet_descriptor::WRITE:
						source.release_packet(packet);
						break;

					case Packet_descriptor::SYNC:
						handle.queued_sync_packet = packet;
						handle.queued_sync_state  = Handle_state::Queued_state::ACK;
						break;

					case Packet_descriptor::CONTENT_CHANGED:
						break;

					case Packet_descriptor::WRITE_TIMESTAMP:
						source.release_packet(packet);
						break;
					}
				};

				try {
					if (packet.operation() == Packet_descriptor::CONTENT_CHANGED) {
						_watch_handle_space.apply<Fs_watch_handle>(id, [&] (Fs_watch_handle &handle) {
							handle.path.with_span([&] (Span const &s) {
								_parent_fs.notify_watchers(s); }); });
					} else {
						_handle_space.apply<Fs_vfs_handle>(id, handle_fn);
					}
				}
				catch (Handle_space::Unknown_id) {
					warning("ack for unknown File_system handle ", id); }

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
			_env(env), _parent_fs(parent_fs),
			_label(config.attribute_value("label", Label_string("/"))),
			_fs(_env.env(), _fs_packet_alloc,
			    _label,
			    config.attribute_value("writeable", true),
			    buffer_size(config))
		{
			if (config.has_attribute("root")) {
				warning("vfs: <fs> node uses deprecated 'root' attribute.");
				warning("     Append the root dir to the label instead.");
			}

			_fs.sigh(_signal_handler);
		}

		/*********************************
		 ** Directory-service interface **
		 *********************************/

		Dataspace_capability dataspace(char const *) override
		{
			/* cannot be implemented without blocking */
			return Dataspace_capability();
		}

		void release(char const *, Dataspace_capability) override { }

		Stat_result stat(char const *path, Stat &out) override
		{
			::File_system::Status status;

			try {
				::File_system::Node_handle node = _fs.node(path);
				Fs_handle_guard node_guard(*this, node, _handle_space);
				status = _fs.status(node);
			}
			catch (Out_of_ram)  {
				error("out-of-ram during stat");
				return STAT_ERR_NO_PERM;
			}
			catch (Out_of_caps) {
				error("out-of-caps during stat");
				return STAT_ERR_NO_PERM;
			}
			catch (...) { return STAT_ERR_NO_ENTRY; }

			out = Stat();

			out.size   = status.size;
			out.type   = _node_type(status.type);
			out.rwx    = _node_rwx(status.rwx);
			out.device = (addr_t)this;
			out.modification_time = {
				.ms_since_1970 = status.modification_time.ms_since_1970 };

			return STAT_OK;
		}

		Unlink_result unlink(char const *path) override
		{
			Absolute_path dir_path(path);
			dir_path.strip_last_element();

			Absolute_path file_name(path);
			file_name.keep_only_last_element();

			try {
				::File_system::Dir_handle dir = _fs.dir(dir_path.base(), false);
				Fs_handle_guard dir_guard(*this, dir, _handle_space);

				_fs.unlink(dir, file_name.base() + 1);
			}
			catch (::File_system::Invalid_handle)    { return UNLINK_ERR_NO_ENTRY;  }
			catch (::File_system::Invalid_name)      { return UNLINK_ERR_NO_ENTRY;  }
			catch (::File_system::Lookup_failed)     { return UNLINK_ERR_NO_ENTRY;  }
			catch (::File_system::Not_empty)         { return UNLINK_ERR_NOT_EMPTY; }
			catch (::File_system::Permission_denied) { return UNLINK_ERR_NO_PERM;   }
			catch (::File_system::Unavailable)       { return UNLINK_ERR_NO_ENTRY;  }

			return UNLINK_OK;
		}

		Rename_result rename(char const *from_path, char const *to_path) override
		{
			if ((strcmp(from_path, to_path) == 0) && dir_entry_exists(from_path))
				return RENAME_OK;

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

				Fs_handle_guard from_dir_guard(*this, from_dir, _handle_space);

				::File_system::Dir_handle to_dir = _fs.dir(to_dir_path.base(),
				                                           false);
				Fs_handle_guard to_dir_guard(*this, to_dir, _handle_space);

				_fs.move(from_dir, from_file_name.base() + 1,
				         to_dir,   to_file_name.base() + 1);
			}
			catch (::File_system::Lookup_failed) { return RENAME_ERR_NO_ENTRY; }
			catch (...)                          { return RENAME_ERR_NO_PERM; }

			return RENAME_OK;
		}

		file_size num_dirent(char const *path) override
		{
			if (strcmp(path, "") == 0)
				path = "/";

			try {
				::File_system::Dir_handle dir = _fs.dir(path, false);
				Fs_handle_guard node_guard(*this, dir, _handle_space);

				return _fs.num_entries(dir);
			}
			catch (...) { }
			return 0;
		}

		bool directory(char const *path) override
		{
			try {
				::File_system::Node_handle node = _fs.node(path);
				Fs_handle_guard node_guard(*this, node, _handle_space);

				::File_system::Status status = _fs.status(node);

				return status.directory();
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

		Open_result open(char const *path, unsigned vfs_mode, Vfs_handle **out_handle,
		                 Allocator& alloc) override
		{
			Absolute_path dir_path(path);
			dir_path.strip_last_element();

			Absolute_path file_name(path);
			file_name.keep_only_last_element();

			::File_system::Mode mode;

			switch (vfs_mode & OPEN_MODE_ACCMODE) {
			default:               mode = ::File_system::STAT_ONLY;  break;
			case OPEN_MODE_RDONLY: mode = ::File_system::READ_ONLY;  break;
			case OPEN_MODE_WRONLY: mode = ::File_system::WRITE_ONLY; break;
			case OPEN_MODE_RDWR:   mode = ::File_system::READ_WRITE; break;
			}

			bool const create = vfs_mode & OPEN_MODE_CREATE;

			try {
				::File_system::Dir_handle dir = _fs.dir(dir_path.base(), false);
				Fs_handle_guard dir_guard(*this, dir, _handle_space);

				::File_system::File_handle file = _fs.file(dir,
				                                           file_name.base() + 1,
				                                           mode, create);

				*out_handle = new (alloc)
					Fs_vfs_file_handle(*this, alloc, vfs_mode, _handle_space, file);
			}
			catch (::File_system::Lookup_failed)       { return OPEN_ERR_UNACCESSIBLE;  }
			catch (::File_system::Permission_denied)   { return OPEN_ERR_NO_PERM;       }
			catch (::File_system::Invalid_handle)      { return OPEN_ERR_UNACCESSIBLE;  }
			catch (::File_system::Node_already_exists) { return OPEN_ERR_EXISTS;        }
			catch (::File_system::Invalid_name)        { return OPEN_ERR_NAME_TOO_LONG; }
			catch (::File_system::Name_too_long)       { return OPEN_ERR_NAME_TOO_LONG; }
			catch (::File_system::No_space)            { return OPEN_ERR_NO_SPACE;      }
			catch (::File_system::Unavailable)         { return OPEN_ERR_UNACCESSIBLE;  }
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }

			return OPEN_OK;
		}

		Opendir_result opendir(char const *path, bool create,
		                       Vfs_handle **out_handle, Allocator &alloc) override
		{
			Absolute_path dir_path(path);

			try {
				::File_system::Dir_handle dir = _fs.dir(dir_path.base(), create);

				*out_handle = new (alloc)
					Fs_vfs_dir_handle(*this, alloc, ::File_system::READ_ONLY,
					                  _handle_space, dir);
			}
			catch (::File_system::Lookup_failed)       { return OPENDIR_ERR_LOOKUP_FAILED;       }
			catch (::File_system::Name_too_long)       { return OPENDIR_ERR_NAME_TOO_LONG;       }
			catch (::File_system::Node_already_exists) { return OPENDIR_ERR_NODE_ALREADY_EXISTS; }
			catch (::File_system::No_space)            { return OPENDIR_ERR_NO_SPACE;            }
			catch (::File_system::Permission_denied)   { return OPENDIR_ERR_PERMISSION_DENIED;   }
			catch (Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }

			return OPENDIR_OK;
		}

		Openlink_result openlink(char const *path, bool create,
		                         Vfs_handle **out_handle, Allocator &alloc) override
		{
			/*
			 * Canonicalize path (i.e., path must start with '/')
			 */
			Absolute_path abs_path(path);
			abs_path.strip_last_element();

			Absolute_path symlink_name(path);
			symlink_name.keep_only_last_element();

			try {
				::File_system::Dir_handle dir_handle = _fs.dir(abs_path.base(),
				                                               false);

				Fs_handle_guard from_dir_guard(*this, dir_handle, _handle_space);

				::File_system::Symlink_handle symlink_handle =
				    _fs.symlink(dir_handle, symlink_name.base() + 1, create);

				*out_handle = new (alloc)
					Fs_vfs_symlink_handle(*this, alloc,
					                      ::File_system::READ_ONLY,
					                      _handle_space, symlink_handle);

				return OPENLINK_OK;
			}
			catch (::File_system::Invalid_handle)      { return OPENLINK_ERR_LOOKUP_FAILED; }
			catch (::File_system::Invalid_name)        { return OPENLINK_ERR_LOOKUP_FAILED; }
			catch (::File_system::Lookup_failed)       { return OPENLINK_ERR_LOOKUP_FAILED; }
			catch (::File_system::Node_already_exists) { return OPENLINK_ERR_NODE_ALREADY_EXISTS; }
			catch (::File_system::No_space)            { return OPENLINK_ERR_NO_SPACE; }
			catch (::File_system::Permission_denied)   { return OPENLINK_ERR_PERMISSION_DENIED; }
			catch (::File_system::Unavailable)         { return OPENLINK_ERR_LOOKUP_FAILED; }
			catch (Out_of_ram)  { return OPENLINK_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPENLINK_ERR_OUT_OF_CAPS; }
		}

		void close(Vfs_handle *vfs_handle) override
		{
			Fs_vfs_handle *fs_handle = static_cast<Fs_vfs_handle *>(vfs_handle);

			_fs.close(fs_handle->file_handle());
			destroy(fs_handle->alloc(), fs_handle);
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


		/***************************
		 ** File_system interface **
		 ***************************/

		static char const *name()   { return "fs"; }
		char const *type() override { return "fs"; }
};

#endif /* _INCLUDE__VFS__FS_FILE_SYSTEM_H_ */
