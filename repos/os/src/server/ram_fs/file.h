/*
 * \brief  File node
 * \author Norman Feske
 * \date   2012-04-11
 */

/*
 * Copyright (C) 2012-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__RAM_FS__FILE_H_
#define _INCLUDE__RAM_FS__FILE_H_

/* Genode includes */
#include <file_system_session/file_system_session.h>
#include <file_system_session/connection.h>
#include <base/allocator.h>

/* local includes */
#include <ram_fs/chunk.h>
#include "node.h"

namespace Ram_fs
{
	using File_system::Chunk;
	using File_system::Chunk_index;
	using File_system::file_size_t;
	using File_system::SEEK_TAIL;
	class File;
}


class Ram_fs::File : public Node
{
	private:

		typedef Chunk<4096>                     Chunk_level_3;
		typedef Chunk_index<128, Chunk_level_3> Chunk_level_2;
		typedef Chunk_index<sizeof(void *) * 16,  Chunk_level_2> Chunk_level_1;
		typedef Chunk_index<sizeof(void *) * 16,  Chunk_level_1> Chunk_level_0;

		Chunk_level_0 _chunk;

		file_size_t _length;

		Genode::Constructible<Genode::Allocator_avl>    _other_alloc { };
		Genode::Constructible<File_system::Connection>  _other_fs { };
		Genode::Constructible<File_system::File_handle> _other_file { };

	public:

		File(Allocator &alloc, char const *name, Genode::Env *env = nullptr,
		     char const *cmp_name = nullptr)
		: _chunk(alloc, 0), _length(0)
		{
			Node::name(name);

			if (env && cmp_name) {
				_other_alloc.construct(&alloc);
				_other_fs.construct(*env, *_other_alloc, cmp_name);

				File_system::Dir_handle root_dir = _other_fs->dir("/", false);
				_other_file.construct(_other_fs->file(root_dir, cmp_name,
				                      File_system::READ_WRITE, true));
			}
		}

		void _other_read(size_t len, seek_off_t seek_offset, const char *c)
		{
			if (!_other_file.constructed())
				return;

			::File_system::Session::Tx::Source &source = *_other_fs->tx();
			using ::File_system::Packet_descriptor;

			while (len) {
				size_t const max_packet_size = source.bulk_buffer_size();
				size_t const count = min(max_packet_size, len);

				try {
					Packet_descriptor packet(source.alloc_packet(count),
					                         *_other_file,
					                         Packet_descriptor::READ,
					                         count,
					                         seek_offset);

					if (!source.ready_to_submit()) {
						error("ready to submit - read");
						Lock lock;
						while (true) lock.lock();
					}

					source.submit_packet(packet);

					/* wait for packet */
					packet = source.get_acked_packet();
					if (packet.operation() != Packet_descriptor::READ) {
						error("unexpected ack packet - read");
						Lock lock;
						while (true) lock.lock();
					}

					char * rcv = source.packet_content(packet);
					if (!packet.succeeded() || count != packet.length() ||
					    memcmp(c, rcv, count))
						error("not same content ", Hex(seek_offset), " ",
						      packet.length(), " vs ", count, " ",
						      !packet.succeeded() ? " failed packet" : "");

					source.release_packet(packet);

					len         -= count;
					c           += count;
					seek_offset += count;
				} catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					error("Packet alloc failed - read");
					Lock lock;
					while (true) lock.lock();
				}
			}
		}

		size_t read(char *dst, size_t len, seek_off_t seek_offset) override
		{
			file_size_t const chunk_used_size = _chunk.used_size();

			if (seek_offset == SEEK_TAIL)
				seek_offset = (len < _length) ? (_length - len) : 0;
			else if (seek_offset >= _length)
				return 0;

			/*
			 * Constrain read transaction to available chunk data
			 *
			 * Note that 'chunk_used_size' may be lower than '_length'
			 * because 'Chunk' may have truncated tailing zeros.
			 */
			if (seek_offset + len >= _length)
				len = _length - seek_offset;

			file_size_t read_len = len;

			if (seek_offset + read_len > chunk_used_size) {
				if (chunk_used_size >= seek_offset)
					read_len = chunk_used_size - seek_offset;
				else
					read_len = 0;
			}

			_chunk.read(dst, read_len, seek_offset);

			_other_read(read_len, seek_offset, dst);

			/* add zero padding if needed */
			if (read_len < len)
				memset(dst + read_len, 0, len - read_len);

			return len;
		}

		void _other_write(Genode::size_t len, seek_off_t seek_offset,
		                  char const * buf)
		{
			if (!_other_file.constructed())
				return;

			::File_system::Session::Tx::Source &source = *_other_fs->tx();
			using ::File_system::Packet_descriptor;

			while (len) {
				Genode::size_t const max_packet_size = source.bulk_buffer_size();
				Genode::size_t const count = min(max_packet_size, len);

				if (!source.ready_to_submit()) {
					error("ready to submit - write");
					Lock lock;
					while (true) lock.lock();
				}

				try {
					Packet_descriptor packet_in(source.alloc_packet(count),
					                            *_other_file,
					                            Packet_descriptor::WRITE,
					                            count,
					                            seek_offset);

					memcpy(source.packet_content(packet_in), buf, count);

					/* pass packet to server side */
					source.submit_packet(packet_in);

					len -= count;
					seek_offset += count;
					buf += count;

					/* synchronous write */
					Packet_descriptor const packet = source.get_acked_packet();
					if (packet.operation() != Packet_descriptor::WRITE) {
						error("unexpected ack packet - write");
						Lock lock;
						while (true) lock.lock();
					}
					if (!packet.succeeded() || count != packet.length())
						error("write was incomplete ", seek_offset, " ",
						      packet.length(), " vs ", count, " ",
						      !packet.succeeded() ? " failed packet" : "");

					source.release_packet(packet);
				} catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					error("Packet_alloc_failed - write");
					Lock lock;
					while (true) lock.lock();
				} catch (...) {
					error("unhandled exception - write");
					Lock lock;
					while (true) lock.lock();
				}
			}
		}

		size_t write(char const *src, size_t len, seek_off_t seek_offset) override
		{
			if (seek_offset == SEEK_TAIL)
				seek_offset = _length;

			if (seek_offset + len >= Chunk_level_0::SIZE) {
				len = (Chunk_level_0::SIZE-1) - seek_offset;
				Genode::error(name(), ": size limit ", (long)Chunk_level_0::SIZE, " reached");
			}

			_chunk.write(src, len, (size_t)seek_offset);

			_other_write(len, seek_offset, src);

			/*
			 * Keep track of file length. We cannot use 'chunk.used_size()'
			 * as file length because trailing zeros may by represented
			 * by zero chunks, which do not contribute to 'used_size()'.
			 */
			_length = max(_length, seek_offset + len);

			mark_as_updated();
			return len;
		}

		Status status() override
		{
			Status s;
			s.inode = inode();
			s.size = _length;
			s.mode = File_system::Status::MODE_FILE;
			return s;
		}

		void truncate(file_size_t size) override
		{
			if (size < _chunk.used_size())
				_chunk.truncate(size);

			_length = size;

			mark_as_updated();
		}
};

#endif /* _INCLUDE__RAM_FS__FILE_H_ */
