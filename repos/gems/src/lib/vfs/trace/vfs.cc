/*
 * \brief  File system for trace buffer access
 * \author Sebastian Sumpf
 * \date   2019-06-13
 */

/*
 * Copyright (C) 2019-2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_dataspace.h>
#include <base/attached_ram_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <vfs/dir_file_system.h>
#include <vfs/single_file_system.h>
#include <vfs/value_file_system.h>

#include <os/vfs.h>

#include <trace_session/connection.h>
#include <trace/trace_buffer.h>

#include "directory_dictionary.h"

namespace Vfs_trace {

	using namespace Vfs;
	using namespace Genode;
	using Name = String<32>;

	struct File_system;
	class  Subject;
	class  Trace_buffer_file_system;
	class  Version_file_system;
}


class Vfs_trace::Trace_buffer_file_system : public Single_file_system
{
	private:

		/**
		 * Trace_buffer wrapper
		 */
		struct Trace_entries
		{
			Vfs::Env                   &_env;
			Constructible<Trace_buffer> _buffer { };

			Trace_entries(Vfs::Env &env) : _env(env) { }

			void setup(Dataspace_capability ds)
			{
				_env.env().rm().attach(ds, {
					.size       = { },  .offset    = { },
					.use_at     = { },  .at        = { },
					.executable = { },  .writeable = true
				}).with_result(
					[&] (Genode::Env::Local_rm::Attachment &a) {
						a.deallocate = false;
						_buffer.construct(*(Trace::Buffer *)a.ptr);
					},
					[&] (Genode::Env::Local_rm::Error) {
						error("failed to attach trace buffer"); }
				);
			}

			void flush()
			{
				if (!_buffer.constructed()) return;

				_env.env().rm().detach(addr_t(_buffer->address()));
				_buffer.destruct();
			}

			template <typename FUNC>
			void for_each_new_entry(FUNC && functor, bool update = true) {
				if (!_buffer.constructed()) return;
				_buffer->for_each_new_entry(functor, update);
			}
		};

		enum State { OFF, TRACE, PAUSED } _state { OFF };

		using Policy_id = Trace::Connection::Alloc_policy_result;

		Vfs::Env          &_env;
		Trace::Connection &_trace;
		Policy_id          _policy;
		Trace::Subject_id  _id;
		Trace::Buffer_size _buffer_size { 1024 * 1024 };
		size_t             _stat_size { 0 };
		Trace_entries      _entries { _env };

		void _setup_and_trace()
		{
			_entries.flush();

			_policy.with_result(
				[&] (Trace::Policy_id policy_id) {
					if (_trace.trace(_id, policy_id, _buffer_size).failed())
						error("failed to start tracing");
				},
				[&] (Trace::Connection::Alloc_policy_error) {
					warning("skip tracing because of invalid policy");
				});

			_entries.setup(_trace.buffer(_id));
		}

	public:

		struct File_channel : Vfs::File_channel
		{
			Allocator     &_alloc;
			Trace_entries &_entries;

			File_channel(Allocator &alloc, Trace_entries &entries)
			:
				Vfs::File_channel({ .writeable = false }),
				_alloc(alloc), _entries(entries)
			{ }

			Read_result read(At, Byte_range_ptr const &dst) override
			{
				size_t out_count = 0;
				_entries.for_each_new_entry([&](Trace::Buffer::Entry entry) {
					size_t const size = min(dst.num_bytes - out_count, entry.length());
					memcpy(dst.start + out_count, entry.data(), size);
					out_count += size;

					if (out_count == dst.num_bytes)
						return false;

					return true;
				});

				return out_count;
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return false; }

			void destruct() override { destroy(_alloc, this); }
		};

		Trace_buffer_file_system(Vfs::Env &env, Parent_fs &parent_fs,
		                         Trace::Connection &trace,
		                         Trace::Policy_id policy,
		                         Trace::Subject_id id)
		:
			Single_file_system(parent_fs, {
				.ident = "trace_buffer",
				.name  = "trace_buffer",
				.rwx   = File::RW_TRANSACTIONAL
			}),
			_env(env), _trace(trace), _policy(policy), _id(id)
		{ }


		/***************************
		 ** File-system interface **
		 ***************************/

		Open_result open(char const *path, Open_attr, Allocator &alloc) override
		{
			if (!_single_file(path))
				return Open_error::DENIED;

			try { return *new (alloc) File_channel(alloc, _entries); }
			catch (Out_of_ram)  { return Open_error::OUT_OF_RAM; }
			catch (Out_of_caps) { return Open_error::OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			Stat_result res = Single_file_system::stat(path, out);
			if (res != Stat_result::OK) return res;

			/* update file size */
			if (_state == TRACE)
				_entries.for_each_new_entry([&](Trace::Buffer::Entry entry) {
					_stat_size += entry.length(); return true; }, false);

			out.size = _stat_size;

			return res;
		}


		/***********************
		 ** FS event handlers **
		 ***********************/

		bool resize_buffer(Trace::Buffer_size size)
		{
			if (size.num_bytes == 0) return false;

			_buffer_size = size;

			switch (_state) {
				case TRACE:
					_trace.pause(_id);
					_setup_and_trace();
					break;
				case PAUSED:
					_state = OFF;
					break;
				case OFF:
					break;
			}
			return true;
		}

		void trace(bool enable)
		{
			if (enable) {
				switch (_state) {
					case TRACE:  break;
					case OFF:    _setup_and_trace(); break;
					case PAUSED: _trace.resume(_id); break;

				}
				_state = TRACE;
			} else {
				switch (_state) {
					case OFF:    return;
					case PAUSED: return;
					case TRACE:  _trace.pause(_id); _state = PAUSED; return;
				}
			}
		}
};


struct Vfs_trace::Subject : Dir_file_system, private Vfs::File_system::Factory
{
	Vfs::Env &_env;

	Value_file_system<bool, 6>             _enabled_fs { *this, "enable", "false\n"};
	Value_file_system<Number_of_bytes, 16> _buffer_size_fs { *this, "buffer_size", "1M\n"};
	String<17>                             _buffer_string { _buffer_size_fs.value() };
	Trace_buffer_file_system               _trace_fs;

	Instance::Attempt create(Vfs::Env &, Parent_fs &, Node const &node) override
	{
		if (_enabled_fs    .matches(node)) return { *this, { _enabled_fs     } };
		if (_buffer_size_fs.matches(node)) return { *this, { _buffer_size_fs } };
		if (_trace_fs      .matches(node)) return { *this, { _trace_fs       } };

		return Error::DENIED;
	}

	void _free(Instance &) override { };

	void notify_watchers(Span const &rel_path) override
	{
		if (rel_path.equals(Span::from_cstring("/enable")))
			_enable_subject();

		if (rel_path.equals(Span::from_cstring("/buffer_size")))
			_buffer_size();

		Dir_file_system::notify_watchers(rel_path);
	}

	void _enable_subject()
	{
		_enabled_fs.value(_enabled_fs.value() ? "true\n" : "false\n");
		_trace_fs.trace(_enabled_fs.value());
	}

	void _buffer_size()
	{
		Trace::Buffer_size const size { _buffer_size_fs.value() };

		if (_trace_fs.resize_buffer(size) == false) {
			/* restore old value */
			_buffer_size_fs.value(_buffer_string);
			return;
		}

		_buffer_size_fs.with_span([&] (Span const &s) { _buffer_string = { s }; });
	}

	using Config = String<200>;

	static Name _name(Node const &n) { return n.attribute_value("name", Name()); }

	static Config _config(Node const &node)
	{
		char buf[Config::capacity()] { };

		Generator::generate({ buf, sizeof(buf) }, "dir",
			[&] (Generator &g) {
				g.attribute("name", _name(node));
				g.named_node("value", "enable");
				g.named_node("value", "buffer_size");
				g.node("trace_buffer");
		}).with_error([] (Genode::Buffer_error) {
			warning("VFS-trace compound exceeds maximum buffer size");
		});

		return Config(Cstring(buf));
	}

	Subject(Vfs::Env &env, Parent_fs &parent_fs, Trace::Connection &trace,
	        Trace::Policy_id policy, Node const &node)
	:
		Dir_file_system(env, parent_fs, _name(node), Ident { { "dir ", _name(node) } }),
		_env(env), _trace_fs(env, *this, trace, policy, { node.attribute_value("id", 0u) })
	{
		_config(node).with_span([&] (Span const &s) {
			Dir_file_system::update(Node(s), *this); });
	}

	~Subject()
	{
		detach_channels(Span::from_cstring("/"));
		Dir_file_system::update(Node(), *this);
	}

	Progress update(Node const &node, Vfs::File_system::Factory &) override
	{
		return _config(node).with_span([&] (Span const &s) {
			return Dir_file_system::update(Node(s), *this); });
	}
};


class Vfs_trace::Version_file_system : public Value_file_system<unsigned>
{
	private:

		unsigned _version { value() };

	public:

		using Value_file_system::Value_file_system;

		bool version_changed()
		{
			Stat s { };
			stat("", s);

			if (s.size == 0 || value() == _version) return false;

			value(++_version);

			return true;
		}
};


struct Vfs_trace::File_system : Union_file_system, private Vfs::File_system::Factory
{
	using Policy_id = Trace::Connection::Alloc_policy_result;

	Vfs::Env         &_env;
	Trace::Connection _trace;
	Policy_id         _policy_id = Trace::Connection::Alloc_policy_error::INVALID;
	Trace_directory   _directory { _env.alloc() };

	Version_file_system _version_fs { *this, "version", 1 };

	void _install_null_policy()
	{
		using namespace Genode;
		Constructible<Attached_rom_dataspace> null_policy;

		try {
			null_policy.construct(_env.env(), "null");
			_policy_id = _trace.alloc_policy(Trace::Policy_size { null_policy->size() });
		}
		catch (Out_of_caps) { throw; }
		catch (Out_of_ram)  { throw; }
		catch (...) {
				error("failed to attach 'null' trace policy."
				      "Please make sure it is provided as a ROM module.");
				throw;
		}

		/* copy policy into trace session */
		_policy_id.with_result(
			[&] (Trace::Policy_id const id) {
				Dataspace_capability ds_cap = _trace.policy(id);
				if (ds_cap.valid()) {
					Attached_dataspace dst { _env.env().rm(), ds_cap };
					memcpy(dst.local_addr<char>(), null_policy->local_addr<void*>(), null_policy->size());
				}
			},
			[&] (Trace::Connection::Alloc_policy_error) { }
		);
	}

	size_t _config_session_ram(Node const &config)
	{
		if (!config.has_attribute("ram")) {
			error("mandatory 'ram' attribute missing");
			throw Exception();
		}
		return config.attribute_value("ram", Number_of_bytes(0));
	}

	Instance::Attempt create(Vfs::Env &, Parent_fs &parent_fs, Node const &node) override
	{
		if (node.type() == "dir") {
			auto &fs = *new (_env.alloc()) Dir_file_system(_env, parent_fs, _name(node));
			return { *this, { fs } };
		}

		if (node.type() == "trace_node")
			return _policy_id.convert<Instance::Attempt>(
				[&] (Trace::Policy_id const id) {
					auto &fs = *new (_env.alloc()) Subject(_env, parent_fs, _trace, id, node);
					return Instance::Attempt { *this, { fs } };
				},
				[&] (Trace::Connection::Alloc_policy_error) {
					return Error::DENIED;
				});

		if (_version_fs.matches(node))
			return { *this, { _version_fs } };

		return Error::DENIED;
	}

	void _free(Instance &inst) override { destroy(_env.alloc(), &inst.fs); };

	static Name _name(Node const &n) { return n.attribute_value("name", Name()); }

	Constructible<Attached_ram_dataspace> _vfs_config_ram { };

	bool _resize_config_ram()
	{
		size_t size = _vfs_config_ram->size() * 2;

		_vfs_config_ram.destruct();

		/* limit to 2MB for now */
		if (size > 2ul * 1024 * 1024) return false;

		_vfs_config_ram.construct(_env.env().ram(), _env.env().rm(), size);

		return true;
	}

	Const_byte_range_ptr _config(Trace_directory &directory)
	{
		_update_trace_subjects();

		char  *buf      = _vfs_config_ram->local_addr<char>();
		size_t buf_size = _vfs_config_ram->size();

		while (Generator::generate({ buf, buf_size }, "composite",
		       [&] (Generator &g) {
			       g.named_node("value", "version");
			       directory.generate(g); }).failed()) {
			/* limit reached */
			if (!_resize_config_ram()) {
				warning("VFS-trace node exceeds maximum buffer size (>2MB)");
				return Const_byte_range_ptr(nullptr, 0);
			}

			buf      = _vfs_config_ram->local_addr<char>();
			buf_size = _vfs_config_ram->size();
		}

		return Const_byte_range_ptr(buf, buf_size);
	}

	void _update_trace_subjects()
	{
		_directory.clear();

		unsigned subjects { 0 }, dead { 0 }, good { 0 };
		_trace.for_each_subject_info([&] (Trace::Subject_id   const id,
		                                  Trace::Subject_info const &info) {
			subjects++;
			if (info.state() == Trace::Subject_info::DEAD) {
				dead++;
				return;
			}

			good++;
			_directory.insert(info, id);
		});

		log("retrieved: ", good, "/", subjects, " subjects (", dead,  " dead)");
	}

	File_system(Vfs::Env &vfs_env, Parent_fs &parent_fs, Node const &node)
	:
		Union_file_system(vfs_env, parent_fs, Ident::from_node(node)),
		_env(vfs_env), _trace(vfs_env.env(), _config_session_ram(node), 512*1024)
	{

		_install_null_policy();

		_vfs_config_ram.construct(_env.env().ram(), _env.env().rm(), 512 * 1024);
	}

	~File_system() { Union_file_system::update(Node(), *this); }

	void destruct() override { destroy(_env.alloc(), this); }

	Progress update(Node const &, Vfs::File_system::Factory &) override
	{
		return Union_file_system::update(Node(_config(_directory)), *this);
	}

	void notify_watchers(Span const &rel_path) override
	{
		if (rel_path.equals(Span::from_cstring("/version"))) {
			if (_version_fs.version_changed()) update(Node(), *this);
		}

		Union_file_system::notify_watchers(rel_path);
	}
};


/**************************
 ** VFS plugin interface **
 **************************/

extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		using Fs = Vfs_trace::File_system;

		Instance::Attempt create(Vfs::Env &vfs_env, Vfs::Parent_fs &parent_fs, Node const &node) override
		{
			try {
				auto &fs = *new (vfs_env.alloc()) Fs(vfs_env, parent_fs, node);
				return { *this, { fs } };
			}
			catch (...) { error("could not create 'trace_fs' "); }
			return Error::DENIED;
		}

		void _free(Instance &instance) override { instance.fs.destruct(); };
	};

	static Factory factory;
	return &factory;
}
