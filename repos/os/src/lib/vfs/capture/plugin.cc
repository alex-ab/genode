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

		Name const &_name;

		using Label = Genode::String<64>;
		Label const &_label;

		Genode::Env &_env;

		Capture::Area const _capture_area { 640, 480 };
		Constructible<Capture::Connection> _capture { };
		Constructible<Attached_dataspace>  _capture_ds { };

		unsigned int _open_count { 0 };

		struct Capture_vfs_handle : Single_vfs_handle
		{
			Constructible<Capture::Connection> &_capture;
			Constructible<Attached_dataspace>  &_capture_ds;

			bool notifying = false;
			bool blocked   = false;

			Capture_vfs_handle(Constructible<Capture::Connection> &capture,
			                   Constructible<Attached_dataspace>  &capture_ds,
			                   Directory_service  &ds,
			                   Genode::Allocator  &alloc,
			                   int                 flags)
			:
				Single_vfs_handle(ds, alloc, flags),
				_capture(capture), _capture_ds(capture_ds)
			{ }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }

			Read_result read(At, Byte_range_ptr const &dst) override
			{
				_capture->capture_at(Point(0, 0));

				size_t const len = min(dst.num_bytes, _capture_ds->size());

				Genode::memcpy(dst.start, _capture_ds->local_addr<char>(), len);

				return len;
			}

			void notify_read_ready() override { notifying = true; }

			Ftruncate_result ftruncate(file_size) override { return FTRUNCATE_OK; }
		};

		using Registered_handle = Genode::Registered<Capture_vfs_handle>;
		using Handle_registry   = Genode::Registry<Registered_handle>;

		Handle_registry _handle_registry { };

	public:

		Data_file_system(Parent_fs   &parent_fs,
		                 Name  const &name,
		                 Label const &label,
		                 Genode::Env &env)
		:
			Single_file_system(parent_fs,
			                   Node_type::TRANSACTIONAL_FILE, name.string(),
			                   Node_rwx::rw(), Node()),
			_name(name), _label(label), _env(env)
		{ }

		static const char *name()   { return "data"; }
		char const *type() override { return "data"; }

		Open_result open(char const  *path, unsigned flags,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			if (_open_count == 0) {
				try {
					_capture.construct(_env, _label.string());
				} catch (Genode::Service_denied) {
					return OPEN_ERR_UNACCESSIBLE;
				}
				_capture->buffer({ .px       = _capture_area,
				                   .mm       = { },
				                   .viewport = { { }, _capture_area } });
				_capture_ds.construct(_env.rm(), _capture->dataspace());
			}

			try {
				*out_handle = new (alloc)
					Registered_handle(_handle_registry,
					                  _capture, _capture_ds,
					                  *this, alloc, flags);
				return OPEN_OK;
			}
			catch (Genode::Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Genode::Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}


		void close(Vfs_handle *handle) override
		{
			_open_count--;

			if (_open_count == 0) {
				_capture_ds.destruct();
				_capture.destruct();
			}

			Single_file_system::close(handle);
		}
};


struct Vfs_capture::File_system : Union_file_system, File_system_factory
{
	using Name  = Vfs_capture::Name;
	using Label = Genode::String<64>;

	Label const _label;
	Name  const _name;

	Genode::Env &_env;

	Dir_file_system  _dot_dir_fs;
	Data_file_system _data_fs { *this, _name, _label, _env };

	static Name name(Node const &config)
	{
		return config.attribute_value("name", Name("capture"));
	}

	Vfs::File_system *create(Vfs::Env&, Parent_fs &, Node const &node) override
	{
		if (node.has_type("dir"))  return &_dot_dir_fs;
		if (node.has_type("data")) return &_data_fs;

		return nullptr;
	}

	using Config = String<200>;
	static Config _config(Name const &name)
	{
		char buf[Config::capacity()] { };

		Genode::Generator::generate({ buf, sizeof(buf) }, "compound",
			[&] (Genode::Generator &g) {
				g.node("data", [&] { g.attribute("name", name); });
				g.node("dir",  [&] { g.attribute("name", Name(".", name)); });
		}).with_error([] (Genode::Buffer_error) {
			Genode::warning("VFS-capture compound exceeds maximum buffer size");
		});

		return Config(Genode::Cstring(buf));
	}

	File_system(Vfs::Env &vfs_env, Parent_fs &parent_fs, Node const &node)
	:
		Union_file_system(vfs_env, parent_fs),
		_label(node.attribute_value("label", Label(""))),
		_name(name(node)),
		_env(vfs_env.env()),
		_dot_dir_fs(vfs_env, *this, Dir_file_system::Name(".", _name))
	{
		Union_file_system::update(Node(_config(name(node))), *this);
	}

	static const char *name() { return "capture"; }

	char const *type() override { return name(); }
};


extern "C" Genode::Vfs::File_system_factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system_factory
	{
		Vfs::File_system *create(Vfs::Env &env, Vfs::Parent_fs &parent_fs,
		                         Node const &node) override
		{
			return new (env.alloc()) Vfs_capture::File_system(env, parent_fs, node);
		}
	};

	static Factory f;
	return &f;
}
