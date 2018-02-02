/*
 * \brief  File system replay tool
 * \author Alexander Boettcher
 * \date   2018-02-02
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>

#include <util/string.h>

#include <vfs/file_system_factory.h>
#include <vfs/dir_file_system.h>

class Replay
{
	private:

		Vfs::File_system               &_vfs;
		Vfs::Vfs_handle                *_handle { nullptr };
		Genode::Attached_rom_dataspace  _replay_rom;
		Genode::Xml_node                _node { _replay_rom.xml().sub_node("operation") };
		unsigned long                   _step { 0 };
		bool                            _done { false };
		bool                            _handling { false };

		struct {
			Genode::uint64_t seek_before;
			Genode::uint64_t write_count;
		} _continue_write { 0 , 0 };

		struct {
			Genode::uint64_t seek_before;
			Genode::uint64_t read_count;
		} _continue_read { 0, 0 };

		Replay(const Replay&);
		Replay & operator=(const Replay&);

		char _write_buf[64 * 1024];
		char _read_buf[64 * 1024];

		bool _out()
		{
			Genode::uint64_t seek_before = 0;

			_node.attribute("seek_before").value(&seek_before);

			if (_handle->seek() != seek_before) {
				Genode::error(__func__, " error ", __LINE__, " ", _handle->seek(), "vs", seek_before);
				return false;
			}

			return true;
		}

		bool _read_in()
		{
			Genode::uint64_t seek_before = 0;
			Genode::uint64_t read_count = 0;

			_node.attribute("seek_before").value(&seek_before);
			_node.attribute("read_count").value(&read_count);

			Vfs::File_io_service::Read_result read_result;

			do {
				if (!_continue_read.read_count) {
					if (!_handle->fs().queue_read(_handle, sizeof(_read_buf))) {
//						Genode::error(__func__, " error ", __LINE__);
						return false;
					}
				} else {
					seek_before = _continue_read.seek_before;
					read_count  = _continue_read.read_count;

					_continue_read.seek_before = 0;
					_continue_read.read_count  = 0;
				}

				try {
					Genode::Xml_node next = _node.next();
					Genode::String<16> type("END");
					next.attribute("type").value(&type);
					if (type == "READ_OUT") {
						Genode::uint64_t next_seek_before = 0;
//					Genode::uint64_t next_write_count = 0;
						next.attribute("seek_before").value(&next_seek_before);
//					next.attribute("read_count").value(&next_write_count);

						if (seek_before + read_count > next_seek_before) {
//							Genode::error("fuck fuck");
							read_count = next_seek_before - seek_before;
						}
					}
				} catch (Genode::Xml_node::Nonexistent_sub_node) { }

				if (_handle->seek() != seek_before) {
					Genode::error(__func__, " error ", __LINE__, " ", seek_before, " ", _handle->seek());
					return false;
				}

				Genode::uint64_t toread = read_count > sizeof(_read_buf) ?
				                          sizeof(_read_buf) : read_count;

				Genode::uint64_t read = 0;

				read_result = _handle->fs().complete_read(_handle, _read_buf,
				                                          toread, read);

				if (read_result == Vfs::File_io_service::READ_OK) {
					read_count -= read;
					seek_before += read;

					_handle->advance_seek(read);

					if (read_count == 0)
						return true;
				} else
				if (read_result == Vfs::File_io_service::READ_QUEUED) {
					_continue_read.seek_before = seek_before;
					_continue_read.read_count  = read_count;
					return false;
				}
			} while (read_result == Vfs::File_io_service::READ_OK);

			Genode::error(__func__, " error ", __LINE__, " ", (int)read_result);
			return false;
		}

		bool _write_in()
		{
			Genode::uint64_t seek_before = 0;
			Genode::uint64_t write_count = 0;

			_node.attribute("seek_before").value(&seek_before);
			_node.attribute("write_count").value(&write_count);

			if (_continue_write.write_count) {
				seek_before = _continue_write.seek_before;
				write_count = _continue_write.write_count;
				_continue_write.seek_before = 0;
				_continue_write.write_count = 0;
			} else {
				if (_handle->seek() != seek_before) {
					Genode::error(__func__, " error ", __LINE__);
					return false;
				}
			}

			try {
				Genode::Xml_node next = _node.next();
				Genode::String<16> type("END");
				next.attribute("type").value(&type);
				if (type == "WRITE_OUT") {
					Genode::uint64_t next_seek_before = 0;
//					Genode::uint64_t next_write_count = 0;
					next.attribute("seek_before").value(&next_seek_before);
//					next.attribute("write_count").value(&next_write_count);

					if (seek_before + write_count > next_seek_before) {
//						Genode::error("fuck fuck");
						write_count = next_seek_before - seek_before;
					}
				}
			} catch (Genode::Xml_node::Nonexistent_sub_node) { }

			typedef Vfs::File_io_service::Write_result  Write_result;
			Write_result r;

			Genode::uint64_t towrite = write_count;

			try {
				do {
					Genode::uint64_t const write = towrite > sizeof(_write_buf) ?
					                               sizeof(_write_buf) : towrite;

					Vfs::file_size written = 0;
					r = _handle->fs().write(_handle, _write_buf, write, written);
					if (r != Write_result::WRITE_OK) {
						Genode::error(__func__, " error ", __LINE__);
						return false;
					}
					if (write < written || towrite < written) {
						Genode::error(__func__, " error ", __LINE__, " ", write, " ", written, " ", towrite);
						return false;
					}

//					Genode::log("write ", write, "/", towrite);

					towrite -= written;

					_handle->advance_seek(written);

					if (_handle->seek() == seek_before + write_count)
						return true;

				} while (r == Write_result::WRITE_OK || write_count);
			} catch (Vfs::File_io_service::Insufficient_buffer) {
//				Genode::warning(__func__, " insufficient buffer");
				_continue_write.write_count = towrite;
				_continue_write.seek_before = _handle->seek();
				return false;
			}

			Genode::error(__func__, " error ", __LINE__);
			return false;
		}

		bool _seek()
		{
			Genode::uint64_t   seek_before = 0;
			Genode::uint64_t   seek_after  = 0;
			Genode::uint64_t   seek_count  = 0;
			Genode::String<16> seek_mode   {"unknown"};

			_node.attribute("seek_before").value(&seek_before);
			_node.attribute("seek_after").value(&seek_after);
			_node.attribute("seek_count").value(&seek_count);
			_node.attribute("seek_mode").value(&seek_mode);

			if (seek_mode == "CUR") {
				if (_handle->seek() != seek_before || seek_before + seek_count != seek_after) {
					Genode::error(__func__," error ", __LINE__, " ", _handle->seek(), "==", seek_before, " ", seek_before, "+", seek_count, "==", seek_after);
					return false;
				}
				_handle->advance_seek(seek_count);
			} else
			if (seek_mode == "SET") {
				if (_handle->seek() != seek_before) {
					Genode::error(__func__," error ", __LINE__, " ", _handle->seek(), "==", seek_before);
					return false;
				}
				_handle->seek(seek_count);
			} else {
				Genode::error(__func__, " error ", __LINE__, " ", seek_mode);
				return false;
			}

			if (_handle->seek() != seek_after) {
				Genode::error(__func__, " error ", __LINE__);
				return false;
			}
			return true;
		}

	public:

		Replay(Vfs::File_system &vfs, Genode::Env &env)
		:
			_vfs(vfs),
			_replay_rom(env, "replay.data")
		{
			for (unsigned i = 0; i < sizeof(_write_buf); i++) {
				_write_buf[i] = 0x20 + (i % 0x60);
			}
		}

		void start(Genode::Allocator &alloc, Genode::String<64> &file_name)
		{
			typedef Vfs::Directory_service::Open_result Open_result;

			Open_result res = _vfs.open(file_name.string(),
			                            Vfs::Directory_service::OPEN_MODE_RDWR,
			                            &_handle, alloc);
			if (res != Open_result::OPEN_OK) {
				throw Genode::Exception();
			}

			replay();
		}

		void replay()
		{
			if (_handling) {
//				Genode::error("nested handler call");
				return;
			}

			if (_done)
				return;

			_handling = true;

			bool loop = true;

			while (loop) {
				Genode::String<16> type("END");
				_node.attribute("type").value(&type);

				if ((_step + 2) % 1000 == 0)
					log("node ", type, " line=", _step + 2);

				if (type == "LSEEK") {
					loop = _seek();
				} else
				if (type == "READ_IN") {
					loop = _read_in();
				} else
				if (type == "READ_OUT") {
					loop = _out();
				} else
				if (type == "WRITE_IN") {
					loop = _write_in();
				} else
				if (type == "WRITE_OUT") {
					loop = _out();
				} else {
					Genode::error("unknown operation ", type);
					loop = false;
				}

				if (loop) {
					try {
						_node = _node.next();
						_step ++;
					} catch (Genode::Xml_node::Nonexistent_sub_node) {
						Genode::log("replay done");
						loop = false;
						_done = true;
					}
				}
			}
			_handling = false;
		}
};

struct Io_response_handler : Vfs::Io_response_handler
{
	Replay * replay { nullptr };

	void handle_io_response(Vfs::Vfs_handle::Context *) override
	{
		if (replay)
			replay->replay();
	}
};

void Component::construct(Genode::Env &env)
{
	static Genode::Heap                    heap(env.ram(), env.rm());
	static Genode::Attached_rom_dataspace  config_rom(env, "config");
	static Genode::Xml_node const          config_xml = config_rom.xml();
	static Io_response_handler             io_response_handler;

	static Vfs::Global_file_system_factory global_file_system_factory(heap);
	static Vfs::Dir_file_system            vfs_root(env, heap,
	                                                config_xml.sub_node("vfs"),
	                                                io_response_handler,
	                                                global_file_system_factory,
	                                                Vfs::Dir_file_system::Root());

	static Replay replay(vfs_root, env);

	io_response_handler.replay = &replay;

	Genode::String<64> file_name("/ram/vm.vdi");

	replay.start(heap, file_name);

}
