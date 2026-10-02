/*
 * \brief  Terminal file system
 * \author Christian Prochaska
 * \author Norman Feske
 * \author Christian Helmuth
 * \date   2012-05-23
 */

/*
 * Copyright (C) 2012-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__TERMINAL_FILE_SYSTEM_H_
#define _INCLUDE__VFS__TERMINAL_FILE_SYSTEM_H_

#include <terminal_session/connection.h>
#include <vfs/single_file_system.h>
#include <vfs/dir_file_system.h>
#include <vfs/readonly_value_file_system.h>
#include <os/ring_buffer.h>


namespace Vfs_terminal {

	using namespace Genode;
	using namespace Genode::Vfs;
	using Name = String<64>;

	struct Data_file_system;
	struct File_system;
}


/**
 * File system node for processing the terminal data read/write streams
 */
class Vfs_terminal::Data_file_system : public Single_file_system
{
	public:

		/**
		 * Interface for propagating user interrupts (control-c)
		 */
		struct Interrupt_handler : Interface
		{
			virtual void handle_interrupt() = 0;
		};

	private:

		Vfs::Env &_env;

		Terminal::Connection &_terminal;

		bool const _raw;

		Interrupt_handler &_interrupt_handler;

		enum { READ_BUFFER_SIZE = 4000 };

		using Read_buffer = Ring_buffer<char, READ_BUFFER_SIZE + 1,
		                                Ring_buffer_unsynchronized>;

		Read_buffer _read_buffer { };

		static void _fetch_data_from_terminal(Terminal::Connection &terminal,
		                                      Read_buffer          &read_buffer,
		                                      Interrupt_handler    &interrupt_handler,
		                                      bool                  raw)
		{
			while (terminal.avail()) {

				/*
				 * Copy new data into read buffer, detect user-interrupt
				 * characters (control-c)
				 */
				unsigned const buf_size = read_buffer.avail_capacity();
				if (buf_size == 0)
					break;

				char buf[buf_size];

				size_t const received = terminal.read(buf, buf_size);

				for (size_t i = 0; i < received; i++) {

					char const c = buf[i];

					enum { INTERRUPT = 3 };

					if (c == INTERRUPT && !raw) {
						interrupt_handler.handle_interrupt();
					} else {
						read_buffer.add(c);
					}
				}
			}
		}

		struct File_channel;
		using File_channels = Registry<File_channel>;

		struct File_channel : Vfs::File_channel
		{
			Allocator   &_alloc;
			Data_file_system &_fs;

			File_channels::Element const _elem;

			bool notifying = false;

			File_channel(Allocator &alloc, Attr attr, Data_file_system &fs)
			:
				Vfs::File_channel(attr),
				_alloc(alloc), _fs(fs), _elem(fs._file_channels, *this)
			{ }

			bool read_ready() const override {
				return !_fs._read_buffer.empty(); }

			bool write_ready() const override { return true; }

			void notify_read_ready() override { notifying = true; }

			Read_result read(At, Byte_range_ptr const &dst) override
			{
				if (_fs._read_buffer.empty())
					_fetch_data_from_terminal(_fs._terminal, _fs._read_buffer,
					                          _fs._interrupt_handler, _fs._raw);

				if (_fs._read_buffer.empty())
					return Read_error::RETRY;

				unsigned consumed = 0;
				for (; consumed < dst.num_bytes && !_fs._read_buffer.empty(); consumed++)
					dst.start[consumed] = _fs._read_buffer.get();

				return consumed;
			}

			Write_result write(At, Const_byte_range_ptr const &src) override
			{
				return _fs._terminal.write(src.start, src.num_bytes);
			}

			Resize_result resize(file_size) override { return Resize_result::OK; }

			void destruct() override { destroy(_alloc, this); }
		};

		File_channels _file_channels { };

		Io_signal_handler<Data_file_system> _read_avail_handler {
			_env.env().ep(), *this, &Data_file_system::_handle_read_avail };

		void _handle_read_avail()
		{
			/*
			 * On non-raw sessions, fetch as much data from the terminal as
			 * possible to detect user-interrupt characters (control-c), even
			 * before the VFS client attempts to read from the terminal.
			 *
			 * Note that a user interrupt that follows a large chunk of data
			 * (exceeding the capacity of the read buffer) cannot be detected
			 * without reading the data first. In the case where the VFS client
			 * never reads data (e.g., it just blocks for a timeout),
			 * consecutive user interrupts will never be delivered once such a
			 * situation occurs. This can be provoked by pasting a large amount
			 * of text into the terminal.
			 */
			_fetch_data_from_terminal(_terminal, _read_buffer, _interrupt_handler,
			                          _raw);

			_file_channels.for_each([] (File_channel &c) {
				if (c.notifying) {
					c.notifying = false;
					c.read_ready_response();
				}
			});

			_env.user().wakeup_vfs_user();
		}

	public:

		Data_file_system(Parent_fs            &parent_fs,
		                 Vfs::Env             &env,
		                 Terminal::Connection &terminal,
		                 Interrupt_handler    &interrupt_handler,
		                 bool                  raw)
		:
			Single_file_system(parent_fs, {
				.ident = { { "data" } },
				.name  = "data",
				.rwx   = File::RW_TRANSACTIONAL
			}),
			_env(env), _terminal(terminal), _raw(raw),
			_interrupt_handler(interrupt_handler)
		{
			/* register for read-avail notification */
			_terminal.read_avail_sigh(_read_avail_handler);
		}

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
		{
			if (!_single_file(path))
				return Open_error::DENIED;

			try {
				return *new (alloc)
					File_channel(alloc, { .writeable = attr.writeable }, *this);
			}
			catch (Out_of_ram)  { return Open_error::OUT_OF_RAM; }
			catch (Out_of_caps) { return Open_error::OUT_OF_CAPS; }
		}
};


struct Vfs_terminal::File_system : Union_file_system,
                                   Data_file_system::Interrupt_handler,
                                   private Vfs::File_system::Factory
{
	using Label = String<64>;
	using Name  = Vfs_terminal::Name;

	Label const _label;

	Name const _name;

	Vfs::Env &_env;

	Terminal::Connection _terminal { _env.env(), _label.string() };

	bool const _raw;

	Data_file_system _data_fs { *this, _env, _terminal, *this, _raw };

	struct Info
	{
		Terminal::Session::Size size;

		void print(Output &out) const
		{
			char buf[128] { };
			Generator::generate({ buf, sizeof(buf) }, "terminal",
				[&] (Generator &g) {
					g.attribute("rows",    size.lines());
					g.attribute("columns", size.columns());
			}).with_error([] (Buffer_error) {
				warning("VFS-terminal info exceeds maximum buffer size");
			});
			Genode::print(out, Cstring(buf));
		}
	};

	/**
	 * Number of occurred user interrupts (control-c)
	 */
	unsigned _interrupts = 0;

	Readonly_value_file_system<Info>     _info_fs       { *this, "info",       Info{} };
	Readonly_value_file_system<unsigned> _rows_fs       { *this, "rows",       0 };
	Readonly_value_file_system<unsigned> _columns_fs    { *this, "columns",    0 };
	Readonly_value_file_system<unsigned> _interrupts_fs { *this, "interrupts", _interrupts };

	Io_signal_handler<File_system> _size_changed_handler {
		_env.env().ep(), *this, &File_system::_handle_size_changed };

	void _handle_size_changed()
	{
		Info const info { .size = _terminal.size() };

		_info_fs   .value(info);
		_rows_fs   .value(info.size.lines());
		_columns_fs.value(info.size.columns());
	}

	/**
	 * Interrupt_handler interface
	 */
	void handle_interrupt() override
	{
		_interrupts++;
		_interrupts_fs.value(_interrupts);
	}

	static Name name(Node const &config)
	{
		return config.attribute_value("name", Name("terminal"));
	}

	Instance::Attempt create(Vfs::Env &, Parent_fs &, Node const &node) override
	{
		if (_data_fs      .matches(node)) return { *this, { _data_fs       } };
		if (_info_fs      .matches(node)) return { *this, { _info_fs       } };
		if (_rows_fs      .matches(node)) return { *this, { _rows_fs       } };
		if (_columns_fs   .matches(node)) return { *this, { _columns_fs    } };
		if (_interrupts_fs.matches(node)) return { *this, { _interrupts_fs } };

		return Error::DENIED;
	}

	void _free(Instance &) override { };

	File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &node)
	:
		Union_file_system(env, parent_fs, Ident::from_node(node)),
		_label(node.attribute_value("label", Label(""))),
		_name(name(node)),
		_env(env),
		_raw(node.attribute_value("raw", false))
	{
		_terminal.size_changed_sigh(_size_changed_handler);
		_handle_size_changed();
	}

	~File_system()
	{
		Union_file_system::update(Node(), *this);
	}

	Progress update(Node const &, Vfs::File_system::Factory &) override
	{
		Generated_node config(_env.alloc(), 1024, "compound", [&] (Generator &g) {
			g.node("data");
			g.named_node("readonly_value", "info");
			g.named_node("readonly_value", "rows");
			g.named_node("readonly_value", "columns");
			g.named_node("readonly_value", "interrupts");
		});

		Progress result { };
		config.node.with_result(
			[&] (Node const &node) {
				result = Union_file_system::update(node, *this); },
			[&] (Buffer_error) {
				warning("vfs_terminal: config '", _name, "' exceeds max capacity"); });
		return result;
	}

	static constexpr auto BUILTIN_FS_TYPE = "terminal";
};

#endif /* _INCLUDE__VFS__TERMINAL_FILE_SYSTEM_H_ */
