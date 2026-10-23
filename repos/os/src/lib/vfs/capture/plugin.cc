/*
 * \brief  VFS capture plugin
 * \author Christian Prochaska
 * \date   2021-09-08
 */

/*
 * Copyright (C) 2021-2022 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <capture_session/connection.h>
#include <vfs/single_file_system.h>
#include <vfs/dir_file_system.h>
#include <vfs/readonly_value_file_system.h>
#include <vfs/env.h>


namespace Vfs_capture
{
	using namespace Genode;
	using namespace Vfs;

	using Name = String<64>;

	struct Data_file_system;
	struct File_system;
};


class Vfs_capture::Data_file_system : public Single_file_system
{
	private:

		using Label = Genode::String<64>;
		Label const &_label;

		Genode::Env &_env;

		Capture::Area const _capture_area { 640, 480 };
		Constructible<Capture::Connection> _capture { };
		Constructible<Attached_dataspace>  _capture_ds { };

		unsigned int _open_count { 0 };

		struct File_channel: Vfs::File_channel
		{
			Allocator &_alloc;
			Data_file_system &_fs;

			bool notifying = false;
			bool blocked   = false;

			File_channel(Allocator &alloc, Attr attr, Data_file_system &fs)
			:
				Vfs::File_channel(attr), _alloc(alloc), _fs(fs)
			{ }

			~File_channel()
			{
				_fs._open_count--;

				if (_fs._open_count == 0) {
					_fs._capture_ds.destruct();
					_fs._capture.destruct();
				}
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }

			Read_result read(At, Byte_range_ptr const &dst) override
			{
				_fs._capture->capture_at(Point(0, 0));

				size_t const len = min(dst.num_bytes, _fs._capture_ds->size());

				Genode::memcpy(dst.start, _fs._capture_ds->local_addr<char>(), len);

				return len;
			}

			void notify_read_ready() override { notifying = true; }

			Resize_result resize(file_size) override { return Resize_result::OK; }

			void destruct() override { destroy(_alloc, this); }
		};

	public:

		Data_file_system(Parent_fs   &parent_fs,
		                 Name  const &name,
		                 Label const &label,
		                 Genode::Env &env)
		:
			Single_file_system(parent_fs, {
				.ident = { { "data ", name } },
				.name  = name,
				.rwx   = File::RW_TRANSACTIONAL
			}),
			_label(label), _env(env)
		{ }

		static const char *name() { return "data"; }

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
		{
			if (!_single_file(path))
				return Open_error::DENIED;

			if (_open_count == 0) {
				try {
					_capture.construct(_env, _label.string());
				} catch (Genode::Service_denied) {
					return Open_error::DENIED;
				}
				_capture->buffer({ .px       = _capture_area,
				                   .mm       = { },
				                   .viewport = { { }, _capture_area } });
				_capture_ds.construct(_env.rm(), _capture->dataspace());
			}
			try {
				return *new (alloc)
					File_channel(alloc, { .writeable = attr.writeable }, *this);
			}
			catch (Genode::Out_of_ram)  { return Open_error::OUT_OF_RAM; }
			catch (Genode::Out_of_caps) { return Open_error::OUT_OF_CAPS; }
		}
};


struct Vfs_capture::File_system : Union_file_system, Vfs::File_system::Factory
{
	using Name  = Vfs_capture::Name;
	using Label = Genode::String<64>;

	Label const _label;
	Name  const _name;

	Vfs::Env &_env;

	Dir_file_system  _dot_dir_fs;
	Data_file_system _data_fs { *this, _name, _label, _env.env() };

	static Name name(Node const &config)
	{
		return config.attribute_value("name", Name("capture"));
	}

	Instance::Attempt create(Vfs::Env&, Parent_fs &, Node const &node) override
	{
		if (_dot_dir_fs.matches(node)) return { *this, { _dot_dir_fs } };
		if (_data_fs   .matches(node)) return { *this, { _data_fs    } };

		return Error::DENIED;
	}

	void _free(Instance &) override { };

	using Config = String<200>;
	static Config _config(Name const &name)
	{
		char buf[Config::capacity()] { };

		Genode::Generator::generate({ buf, sizeof(buf) }, "compound",
			[&] (Genode::Generator &g) {
				g.named_node("data", name);
				g.named_node("dir",  Name(".", name));
		}).with_error([] (Genode::Buffer_error) {
			Genode::warning("VFS-capture compound exceeds maximum buffer size");
		});

		return Config(Genode::Cstring(buf));
	}

	File_system(Vfs::Env &vfs_env, Parent_fs &parent_fs, Node const &node)
	:
		Union_file_system(vfs_env, parent_fs, Ident::from_node(node)),
		_label(node.attribute_value("label", Label(""))),
		_name(name(node)),
		_env(vfs_env),
		_dot_dir_fs(_env, *this, Dir_file_system::Name(".", _name))
	{ }

	Progress update(Node const &, Vfs::File_system::Factory &) override
	{
		return _config(_name).with_span([&] (Span const &s) {
			return Union_file_system::update(Node(s), *this); });
	}

	static const char *name() { return "capture"; }

	void destruct() override { destroy(_env.alloc(), this); }
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		using Fs = Vfs_capture::File_system;

		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &parent_fs,
		                         Node const &node) override
		{
			return { *this, { *new (env.alloc()) Fs(env, parent_fs, node) } };
		}

		void _free(Instance &instance) override { instance.fs.destruct(); };
	};

	static Factory f;
	return &f;
}
