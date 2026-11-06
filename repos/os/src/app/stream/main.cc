/*
 * \brief  Utility for forwarding data from one file to another
 * \author Norman Feske
 * \date   2026-11-06
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/component.h>
#include <base/heap.h>
#include <base/attached_rom_dataspace.h>
#include <os/vfs.h>

namespace Stream {
	using namespace Genode;
	struct Main;
}


struct Stream::Main
{
	Env &_env;

	Attached_rom_dataspace _config_rom { _env, "config" };

	using Path = Directory::Path;

	struct Config
	{
		Path in, out;
		size_t buffer;

		static Config from_node(Node const &node)
		{
			return {
				.in     = node.attribute_value("in",  Path()),
				.out    = node.attribute_value("out", Path()),
				.buffer = node.attribute_value("buffer", Num_bytes(16*1024))
			};;
		}

		bool operator != (Config const &other) const
		{
			return in != other.in || out != other.out;
		}

	} _config { };

	Vfs::At _at { };

	Heap _heap { _env.ram(), _env.rm() };

	Root_directory _root { _env, _heap };

	Signal_handler<Main> _config_handler { _env.ep(), *this, &Main::_handle_config };

	Constructible<Readonly_file>       _in    { };
	Constructible<Append_file>         _out   { };
	Constructible<Watch_handler<Main>> _watch { };

	Memory::Allocation::Attempt _buffer = Alloc_error::DENIED;

	void _handle_config()
	{
		_config_rom.update();

		Progress progress { };
		_config_rom.node().with_optional_sub_node("vfs", [&] (Node const &node) {
			progress = _root.apply_config(node); });

		Config const orig_config = _config;
		_config = Config::from_node(_config_rom.node());
		if (orig_config != _config)
			progress = PROGRESSED;

		if (progress.progressed) {
			_in   .construct(_root, _config.in);
			_out  .construct(_root, _config.out, Vfs::Timestamp { });
			_watch.construct(_env.ep(), _root, _config.in, *this, &Main::_handle_watch);
			_buffer = _heap.try_alloc(_config.buffer);
			_at     = { };
			_copy();
		}
	}

	void _handle_watch() { _copy(); }

	void _copy()
	{
		if (!_in.constructed() || !_out.constructed()) return;

		_buffer.with_result(
			[&] (Memory::Allocation &a) {
				_copy(*_in, *_out, { (char *)a.ptr, a.num_bytes });
			},
			[&] (Alloc_error) { });
	}

	void _copy(Readonly_file &in, Append_file &out, Byte_range_ptr const &buf)
	{
		for (;;) {
			size_t const bytes = in.read(_at, buf);

			if (bytes == 0)
				break;

			if (out.append(buf.start, bytes) == Writeable_file::Append_result::WRITE_ERROR) {
				error("unable to write to ", _config.out);
				return;
			}

			_at.pos += bytes;
		}
	}

	Main(Env &env) : _env(env)
	{
		_config_rom.sigh(_config_handler);
		_config_handler.local_submit();
	}
};


void Component::construct(Genode::Env &env) { static Stream::Main main(env); }
