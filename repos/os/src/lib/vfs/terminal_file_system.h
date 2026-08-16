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

		Name const _name;

		Entrypoint &_ep;

		Vfs::Env::User &_vfs_user;

		Terminal::Connection &_terminal;

		Interrupt_handler &_interrupt_handler;

		bool const _raw;

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

		struct Terminal_vfs_handle : Single_vfs_handle
		{
			Terminal::Connection &_terminal;
			Vfs::Env::User       &_vfs_user;
			Read_buffer          &_read_buffer;
			Interrupt_handler    &_interrupt_handler;

			bool const _raw;

			bool notifying = false;

			Terminal_vfs_handle(Terminal::Connection &terminal,
			                    Vfs::Env::User       &vfs_user,
			                    Read_buffer          &read_buffer,
			                    Interrupt_handler    &interrupt_handler,
			                    Directory_service    &ds,
			                    Allocator            &alloc,
			                    int                   flags,
			                    bool                  raw)
			:
				Single_vfs_handle(ds, alloc, flags),
				_terminal(terminal),
				_vfs_user(vfs_user),
				_read_buffer(read_buffer),
				_interrupt_handler(interrupt_handler),
				_raw(raw)
			{ }

			bool read_ready() const override {
				return !_read_buffer.empty(); }

			bool write_ready() const override { return true; }

			void notify_read_ready() override { notifying = true; }

			Read_result read(At, Byte_range_ptr const &dst) override
			{
				if (_read_buffer.empty())
					_fetch_data_from_terminal(_terminal, _read_buffer,
					                          _interrupt_handler, _raw);

				if (_read_buffer.empty())
					return Read_error::RETRY;

				unsigned consumed = 0;
				for (; consumed < dst.num_bytes && !_read_buffer.empty(); consumed++)
					dst.start[consumed] = _read_buffer.get();

				return consumed;
			}

			Write_result write(At, Const_byte_range_ptr const &src) override
			{
				return _terminal.write(src.start, src.num_bytes);
			}

			Ftruncate_result ftruncate(file_size) override
			{
				return FTRUNCATE_OK;
			}
		};

		using Registered_handle = Registered<Terminal_vfs_handle>;
		using Handle_registry   = Registry<Registered_handle>;

		Handle_registry _handle_registry { };

		Io_signal_handler<Data_file_system> _read_avail_handler {
			_ep, *this, &Data_file_system::_handle_read_avail };

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

			_handle_registry.for_each([] (Registered_handle &handle) {
				if (handle.notifying) {
					handle.notifying = false;
					handle.read_ready_response();
				}
			});

			_vfs_user.wakeup_vfs_user();
		}

	public:

		Data_file_system(Parent_fs            &parent_fs,
		                 Entrypoint           &ep,
		                 Vfs::Env::User       &vfs_user,
		                 Terminal::Connection &terminal,
		                 Name           const &name,
		                 Interrupt_handler    &interrupt_handler,
		                 bool                  raw)
		:
			Single_file_system(parent_fs,
			                   Node_type::TRANSACTIONAL_FILE, name.string(),
			                   Node_rwx::rw(), Node()),
			_name(name), _ep(ep), _vfs_user(vfs_user), _terminal(terminal),
			_interrupt_handler(interrupt_handler),
			_raw(raw)
		{
			/* register for read-avail notification */
			_terminal.read_avail_sigh(_read_avail_handler);
		}

		static const char *name()   { return "data"; }
		char const *type() override { return "data"; }

		Open_result open(char const  *path, unsigned flags,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				*out_handle = new (alloc)
					Registered_handle(_handle_registry, _terminal, _vfs_user,
					                  _read_buffer, _interrupt_handler,
					                  *this, alloc, flags, _raw);
				return OPEN_OK;
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}
};


struct Vfs_terminal::File_system : Union_file_system, File_system_factory,
                                   Data_file_system::Interrupt_handler
{
	using Label = String<64>;
	using Name  = Vfs_terminal::Name;

	Label const _label;

	Name const _name;

	Genode::Env &_env;

	Vfs::Env::User &_vfs_user;

	Terminal::Connection _terminal { _env, _label.string() };

	bool const _raw;

	Data_file_system _data_fs { *this, _env.ep(), _vfs_user, _terminal, _name, *this, _raw };

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

	Dir_file_system _dot_dir_fs;

	Readonly_value_file_system<Info>     _info_fs       { *this, "info",       Info{} };
	Readonly_value_file_system<unsigned> _rows_fs       { *this, "rows",       0 };
	Readonly_value_file_system<unsigned> _columns_fs    { *this, "columns",    0 };
	Readonly_value_file_system<unsigned> _interrupts_fs { *this, "interrupts", _interrupts };

	Io_signal_handler<File_system> _size_changed_handler {
		_env.ep(), *this, &File_system::_handle_size_changed };

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

	Vfs::File_system *create(Vfs::Env &, Parent_fs &, Node const &node) override
	{
		if (node.has_type("dir"))        return &_dot_dir_fs;
		if (node.has_type("data"))       return &_data_fs;
		if (node.has_type("info"))       return &_info_fs;
		if (node.has_type("rows"))       return &_rows_fs;
		if (node.has_type("columns"))    return &_columns_fs;
		if (node.has_type("interrupts")) return &_interrupts_fs;

		return nullptr;
	}

	using Config = String<200>;
	static Config _config(Vfs_terminal::Name const &name)
	{
		char buf[Config::capacity()] { };

		Generator::generate({ buf, sizeof(buf) }, "compound",
			[&] (Generator &g) {

				g.node("data", [&] {
					g.attribute("name", name); });

				g.node("dir", [&] {
					g.attribute("name", Dir_file_system::Name(".", name));
					g.node("info");
					g.node("rows");
					g.node("columns");
					g.node("interrupts");
				});

		}).with_error([] (Buffer_error) {
			warning("VFS-terminal compound exceeds maximum buffer size");
		});

		return Config(Cstring(buf));
	}

	File_system(Vfs::Env &vfs_env, Parent_fs &parent_fs, Node const &node)
	:
		Union_file_system(vfs_env, parent_fs),
		_label(node.attribute_value("label", Label(""))),
		_name(name(node)),
		_env(vfs_env.env()),
		_vfs_user(vfs_env.user()),
		_raw(node.attribute_value("raw", false)),
		_dot_dir_fs(vfs_env, *this, Dir_file_system::Name(".", _name))
	{
		_terminal.size_changed_sigh(_size_changed_handler);
		_handle_size_changed();
		Union_file_system::update(Node(_config(name(node))), *this);
	}

	static const char *name() { return "terminal"; }

	char const *type() override { return name(); }
};

#endif /* _INCLUDE__VFS__TERMINAL_FILE_SYSTEM_H_ */
