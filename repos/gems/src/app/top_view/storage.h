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
	using namespace Genode;
	using namespace File_system;

	typedef File_system::Packet_descriptor Packet;
	typedef File_system::Session           Session;
}

struct Type_a
{
	Genode::Trace::Subject_id      id;
	Genode::Trace::Execution_time  execution_time;
};

class Top::Storage
{
	private:

		Env &env;
		Heap                  heap { env.pd(), env.rm() };
		Allocator_avl    avl_alloc { &heap };
		File_system::Connection fs { env, avl_alloc, "store", "/", true };
		Session::Tx::Source    &tx { *fs.tx() };
		Dir_handle      dir_handle { fs.dir("/", false) };
		File_handle file_handle    { fs.file(dir_handle, "trace.top",
		                                     READ_WRITE, true /* create */) };
		uint64_t     _fs_offset    { 0 };

		size_t       _packet_pos   { 0 };
		size_t const _packet_max   { tx.bulk_buffer_size() / Session::TX_QUEUE_SIZE };
		Packet       _packet       { _alloc_packet(_packet_max) };

		Signal_handler<Storage> _handler { env.ep(), *this, &Storage::handle_fs };

		void handle_fs()
		{
#if 0
			Genode::error("handle_fs");
#endif
			while (tx.ack_avail()) {
				auto packet = tx.get_acked_packet();
				tx.release_packet(packet);
			}
		}

		Packet _alloc_packet(size_t const size)
		{
			return Packet(tx.alloc_packet(size), file_handle,
			              Packet::WRITE, size, _fs_offset);
		}

		bool _write(void *data, size_t size)
		{
			if (_packet_pos + size >= _packet_max) {
#if 0
				Genode::error("alloc packet rest=", _packet_pos + size - _packet_max,
				              " send=", _packet_pos,
				              " fs_offset=", _fs_offset);
#endif
				if (!_send_packet(_packet_max))
					return false;

				try {
					_packet     = _alloc_packet(_packet_max);
				} catch (Session::Tx::Source::Packet_alloc_failed) {
					return false;
				}
				_packet_pos = 0;
			}

			memcpy(((char *)tx.packet_content(_packet)) + _packet_pos, data, size);
			_packet_pos += size;

			return true;
		}

		bool _send_packet(size_t const size)
		{
			if (!tx.ready_to_submit())
				return false;

			tx.submit_packet(_packet);

			_fs_offset += size;

			return true;
		}

	public:

		Storage(Env &env) : env(env)
		{
			fs.sigh_ready_to_submit(_handler);
			fs.sigh_ack_avail(_handler);
		}

		void write(Type_a value)
		{
			if (!_write(&value, sizeof(value)))
				Genode::error("packet lost ", _fs_offset);
		}
};
