/*
 * \brief  Storage handling
 * \author Alexander Boettcher
 * \date   2019-04-18
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <file_system_session/connection.h>

namespace Top {
	class Storage;
	class File;

	using namespace Genode;
	using namespace File_system;

	typedef File_system::Packet_descriptor Packet;
	typedef File_system::Session           Session;
}

struct Type_a
{
	Genode::Trace::Subject_id      id;
	Genode::Trace::Execution_time  execution_time;
	Genode::uint16_t               part_ec_time;
	Genode::uint16_t               part_sc_time;
};

struct Type_b
{
	Genode::Trace::Subject_id id;
	Genode::Session_label     label;
};

class Top::File
{
	private:

		File_system::Connection &_fs;
		File_handle              _file_handle;
		uint64_t                 _fs_offset { 0 };
		size_t                   _pos       { 0 };

		size_t const             _max;
		char                     _buffer [8192];

	public:

		File (File_system::Connection &fs, char const *file, size_t max)
		:
			_fs(fs),
			_file_handle { _fs.file(_fs.dir("/", false), file,
			               READ_WRITE, true /* create */) },
			_max(max > sizeof(_buffer) ? sizeof(_buffer) : max)
		{ }

		bool write(void *data, size_t const size)
		{
			if (!size || size > _max || _pos + size >= _max)
				return false;

			memcpy(_buffer + _pos, data, size);

			_pos       += size;
			_fs_offset += size;

			return true;
		}

		Packet flush_data(Session::Tx::Source &tx)
		{
			Packet packet { tx.alloc_packet(_pos), _file_handle,
			                Packet::WRITE, _pos, _fs_offset - _pos};

			memcpy(((char *)tx.packet_content(packet)), _buffer, _pos);

#if 0
			error(this, " fs_offset=", _fs_offset, " send=", _pos);
#endif
			_pos = 0;

			return packet;
		}

		bool flush(size_t const space) const { return _pos + space >= _max; }
		bool empty() const { return _pos == 0; }
};

class Top::Storage
{
	private:

		Env &env;
		Heap heap { env.pd(), env.rm() };
		Allocator_avl avl_alloc { &heap };
		File_system::Connection fs { env, avl_alloc, "store", "/", true };
		Session::Tx::Source  &tx { *fs.tx() };

		size_t const _packet_max { tx.bulk_buffer_size() / Session::TX_QUEUE_SIZE };

		Top::File    _data       { fs, "data.top_view", _packet_max };
		Top::File    _subject    { fs, "subject.top_view", _packet_max };

		Signal_handler<Storage> _handler { env.ep(), *this, &Storage::handle_fs };

		void handle_fs()
		{
			while (tx.ack_avail()) {
				auto packet = tx.get_acked_packet();
				tx.release_packet(packet);
			}
		}

	public:

		Storage(Env &env) : env(env)
		{
			fs.sigh_ready_to_submit(_handler);
			fs.sigh_ack_avail(_handler);
		}

		void write(Type_a value)
		{
			if (!_data.write(&value, sizeof(value)))
				error("data lost - buffer full");

			/* ask for whether flushing is appropriate */
			if (!_data.flush(sizeof(value) * 2)) return;

			if (!tx.ready_to_submit())
			{
				/* check for available acks */
				handle_fs();
				if (!tx.ready_to_submit())
					warning("not ready for submitting - blocking ahead");
			}

			try {
				Packet packet = _data.flush_data(tx);
				tx.submit_packet(packet);
			} catch (Session::Tx::Source::Packet_alloc_failed) {
				error("packet lost"); }
		}

		void write(Type_b value)
		{
			if (!_subject.write(&value, sizeof(value)))
				error("data lost - buffer full");

			/* ask for whether flushing is appropriate */
			if (!_subject.flush(sizeof(value) * 2)) return;

			if (!tx.ready_to_submit())
			{
				/* check for available acks */
				handle_fs();
				if (!tx.ready_to_submit())
					warning("not ready for submitting - blocking ahead");
			}

			try {
				Packet packet = _subject.flush_data(tx);
				tx.submit_packet(packet);
			} catch (Session::Tx::Source::Packet_alloc_failed) {
				error("packet lost"); }
		}

		void force_data_flush()
		{
			if (_data.empty()) return;

			try {
				Packet packet = _data.flush_data(tx);
				tx.submit_packet(packet);
			} catch (Session::Tx::Source::Packet_alloc_failed) {
				error("packet lost during flush"); }
		}
};
