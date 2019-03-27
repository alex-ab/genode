/*
 * \brief  Storage handling
 * \author Alexander Boettcher
 * \date   2019-06-15
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <file_system_session/connection.h>

namespace Top
{
	template <typename> class Storage;
	class File;

	using namespace Genode;
	using namespace File_system;

	typedef File_system::Packet_descriptor Packet;
	typedef File_system::Session           Session;
}

class Top::File
{
	private:

		File_system::Connection &_fs;
		File_handle              _file_handle;
		uint64_t                 _fs_offset { 0 };
		uint64_t                 _fs_size   { 0 };
		size_t                   _pos       { 0 };

		size_t const             _max;
		char                     _buffer [8192];

	public:

		File (File_system::Connection &fs, char const *file, size_t max)
		:
			_fs(fs),
			_file_handle { _fs.file(_fs.dir("/", false), file,
			               READ_ONLY, false /* create */) },
			_max(max > sizeof(_buffer) ? sizeof(_buffer) : max)
		{ }

		void read(Session::Tx::Source &tx)
		{
			if (_fs_offset > _fs_size) return;
			size_t const request = (_fs_size - _fs_offset < _max) ? _fs_size - _fs_offset : _max;
			if (!request) return;

//			Genode::log(this, " gogo ! ", _fs_offset, "->", _fs_offset + request, " fs_size==", _fs_size, " request=", request);

			Packet packet { tx.alloc_packet(request), _file_handle,
			                Packet::READ, request, _fs_offset };

			_fs_offset += request;

			tx.submit_packet(packet);
		}

		bool update_fs_size(uint64_t size)
		{
			bool const size_change = (size != _fs_size);
			_fs_size = size;
			return size_change;
		}

		void adjust_offset(long long const adjust) { _fs_offset += adjust; }

		File_handle file_handle() const { return _file_handle; }
};

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

template <typename T>
class Top::Storage
{
	private:

		Env &env;
		T &_notify;
		Heap heap { env.pd(), env.rm() };
		Allocator_avl avl_alloc { &heap };
		File_system::Connection _fs { env, avl_alloc, "load", "/", false };
		Session::Tx::Source  &tx { *_fs.tx() };

		size_t const _packet_max { tx.bulk_buffer_size() / Session::TX_QUEUE_SIZE };

		Genode::Constructible<Top::File> _data    { };
		Genode::Constructible<Top::File> _subject { };

		Signal_handler<Storage> _handler { env.ep(), *this, &Storage::_handle_fs_event };

		Genode::uint64_t _current_timestamp { 0 };

		void _handle_fs_event()
		{
//			Genode::error("something happened");

			bool read_subject = false;
			bool read_data = false;

			while (tx.ack_avail()) {
				auto packet = tx.get_acked_packet();
				if (packet.operation() != File_system::Packet_descriptor::Opcode::READ) {
					tx.release_packet(packet);
					continue;
				}

				if (!packet.succeeded()) {
					Genode::warning("not succeeded read packet ?");
					tx.release_packet(packet);
					continue;
				}

				if (_subject.constructed() && _subject->file_handle() == packet.handle()) {
//					read_subject = true;
					unsigned offset = (packet.position() % sizeof(Type_b));
					if (offset > 0) offset = sizeof(Type_b) - offset;

					Type_b * value = reinterpret_cast<Type_b *>(reinterpret_cast<char *>(tx.packet_content(packet)) + offset);
					char const * const x = reinterpret_cast<char const * >(tx.packet_content(packet)) + packet.length();
					char const * const y = reinterpret_cast<char const * >(value);
					unsigned const count = (x - y) / sizeof(*value);
					for (unsigned i = 0; i < count; i++) {
						Genode::log("subject - ", Genode::Hex(value[i].id.id), " '", value[i].label,"'");
					}
					Genode::log("subject - lost pre ", offset, " post ", (x - y) % sizeof(*value), " size(value)=", sizeof(*value));
				}

				if (_data.constructed() && _data->file_handle() == packet.handle()) {
					read_data = true;

					unsigned offset = (packet.position() % sizeof(Type_a));

					if (offset > 0)
						Genode::warning("data: unexpected offset detected ...");

					if (offset > 0) offset = sizeof(Type_a) - offset;

					Type_a const * const value = reinterpret_cast<Type_a *>(reinterpret_cast<char *>(tx.packet_content(packet)) + offset);
					char const * const x = reinterpret_cast<char const * >(tx.packet_content(packet)) + packet.length();
					char const * const y = reinterpret_cast<char const * >(value);
					unsigned const count = (x - y) / sizeof(*value);
					unsigned applied_count = 0;
					unsigned drop_time = 0;

					enum SORT_TIME { EC_TIME = 0, SC_TIME = 1} sort = { EC_TIME };

					for (unsigned i = 0; i < count; i++) {
#if 0
						Genode::log("data    - ", Genode::Hex(value[i].id.id), " ", Genode::Hex(value[i].execution_time.thread_context),
						            " ec diff ", Genode::Hex(value[i].part_ec_time),
						            " sc diff ", Genode::Hex(value[i].part_sc_time));
#endif

						/* XXX - id of new data should be 0 when on newest branch of Genode */
						if (value[i].id.id == ~0U) {
							_current_timestamp = value[i].execution_time.thread_context;

							if (!_notify.advance_column_by_storage(_current_timestamp)) {
//								Genode::log("---- ", i, "/", count, " packet.length=", packet.length(), " .position=", packet.position(), " ", sizeof(Type_a), " ", (packet.length() - offset - (i*sizeof(*value))), " ");

								_data->adjust_offset(-1LL * (packet.length() - offset - (i*sizeof(*value))));

								/* stop reading new packets */
								tx.release_packet(packet);
								return;
							}

							if (value[i].execution_time.scheduling_context == EC_TIME)
								sort = EC_TIME;
							else
								sort = SC_TIME;
						}

						if (_current_timestamp < _notify.time()) {
							drop_time ++;
							continue;
						}

						uint64_t const time = (sort == EC_TIME) ? value[i].part_ec_time : value[1].part_sc_time;
						if (_notify.new_data(time, value[i].id.id, _current_timestamp))
							applied_count++;
					}

					if ((x - y) % sizeof(*value))
						_data->adjust_offset(-1LL * ((x - y) % sizeof(*value)));

				}
				tx.release_packet(packet);
			}

			if (read_subject)
				_subject->read(*_fs.tx());

			if (read_data)
				_data->read(*_fs.tx());
		}

		void _handle_fs()
		{
			if (_data.constructed())
				_data->read(*_fs.tx());

#if 0
			if (_subject.constructed())
				_subject->read(*_fs.tx());
#endif
		}

	public:

		Storage(Env &env, T &notify) : env(env), _notify(notify)
		{
			_fs.sigh_ready_to_submit(_handler);
			_fs.sigh_ack_avail(_handler);

			_fs.watch("/");
		}

		void ping()
		{
			bool invoke_handle_fs = false;

			try {
				char const * file = "data.top_view";
				char const * filex = "/data.top_view";
				File_system::Node_handle node   = _fs.node(filex);
				File_system::Status      status = _fs.status(node);
				_fs.close(node);

//				Genode::log(filex, " size=", status.size);

				if (!_data.constructed()) {
					Genode::log("opening ", file);
					try {
						_data.construct(_fs, file, _packet_max);
					} catch (...) { warning(file, " not available"); };
				}

				if (_data.constructed()) {
					if (_data->update_fs_size(status.size))
						invoke_handle_fs = true;
				}
			} catch (...) { }

			try {
				char const * file = "subject.top_view";
				char const * filex = "/subject.top_view";
				File_system::Node_handle node   = _fs.node(filex);
				File_system::Status      status = _fs.status(node);
				_fs.close(node);

//				Genode::log(filex, " size=", status.size);

				if (!_subject.constructed()) {
					Genode::log("opening ", file);
					try {
						_subject.construct(_fs, file, _packet_max);
					} catch (...) { warning(file, " not available"); };
				}

				if (_subject.constructed()) {
					if (_subject->update_fs_size(status.size))
						invoke_handle_fs = true;
				}
			} catch (...) { }

			if (invoke_handle_fs)
				_handle_fs();
		}
};
