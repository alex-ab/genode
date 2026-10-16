/*
 * \brief  Device file system
 * \author Norman Feske
 * \date   2026-10-01
 *
 * The device file system presents a device file alongside a .device/
 * directory. The .device directory hosts one other file system specified
 * as child node in the configuration. This hosted file system implements
 * the device interface in the form of pseudo files. The device file is
 * an alias for a data file within the .device/ directory. The data file
 * is named "data" if present. If not present, the first directory entry
 * is assumed to be the data file.
 *
 * The device file is marked as 'Dirent_type::DEVICE', indicating to the
 * libc that a sibling .device/ directory exists for accessing device
 * operations (ioctl).
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _VFS__DEVICE_FILE_SYSTEM_H_
#define _VFS__DEVICE_FILE_SYSTEM_H_

#include <vfs/union_file_system.h>


namespace Vfs_device {

	using namespace Genode;
	using namespace Genode::Vfs;

	struct File_system;
}


struct Vfs_device::File_system : Union_file_system, private Vfs::File_system::Factory
{
	Vfs::Env &_env;

	using Name  = String<64>;
	Name const _name;

	static Name name(Node const &node)
	{
		Name result { };
		node.for_each_sub_node([&] (Node const &n) { result = n.type(); });
		return node.attribute_value("name", result);
	}

	Name _data_file_name { };

	Node_rwx _rwx { };

	using Path = String<MAX_PATH_LEN>;

	Path _dot_dir_path(Name const &file) const { return { "/.", _name, "/", file }; }

	Path _data_file_path() const { return _dot_dir_path(_data_file_name); }

	struct Device_file_system : Vfs::File_system
	{
		using File_system = Vfs_device::File_system;

		File_system &_fs;

		struct Dir_channel : Vfs::Dir_channel
		{
			File_system &_fs;
			Allocator   &_alloc;

			Dir_channel(File_system &fs, Allocator &a) : _fs(fs), _alloc(a) { }

			void destruct() override { destroy(_alloc, this); }

			Read_dir_result read_dir(At const at, Dirent_range_ptr const &dst) override
			{
				if (dst.num_entries < 1)
					return Read_dir_error::DENIED;

				if (at.pos > 0)
					return Read_dir_end();

				dst.start[0] = {
					.type = Dirent_type::DEVICE,
					.rwx  = _fs._rwx,
					.name = { _fs._name.string() }
				};
				return 1;
			}
		};

		bool _matches(const char *path)
		{
			return (strlen(path) == (strlen(_fs._name.string()) + 1)) &&
			       (strcmp(&path[1], _fs._name.string()) == 0);
		}

		bool _root(const char *path)
		{
			return (strcmp(path, "") == 0) || (strcmp(path, "/") == 0);
		}

		unsigned num_dirent(char const *path) override { return _root(path) ? 1 : 0; }

		bool directory(char const *path) override { return _root(path); }

		bool dir_entry_exists(char const *path) override { return _matches(path); }

		Device_file_system(File_system &fs)
		:
			Vfs::File_system(Ident { { "device ", fs._name } }), _fs(fs)
		{ }

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
		{
			if (!_matches(path))
				return Open_error::DENIED;

			return _fs._dot_dir_fs.open(_fs._data_file_path().string(), attr, alloc);
		}

		Opendir_result opendir(char const *path, Allocator &alloc) override
		{
			if (!_root(path))
				return Opendir_error::DENIED;

			try { return *new (alloc) Dir_channel(_fs, alloc); }
			catch (Out_of_ram)  { return Opendir_error::OUT_OF_RAM;  }
			catch (Out_of_caps) { return Opendir_error::OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			out = Stat { };

			if (_root(path)) {
				out.type = Dirent_type::DIRECTORY;
			} else if (_matches(path)) {
				Stat_result result = _fs._dot_dir_fs.stat(_fs._data_file_path().string(), out);
				out.type = Dirent_type::DEVICE;
				out.rwx  = _fs._rwx;
				return result;
			}
			return Stat_result::DENIED;
		}

		Follow_result follow(char const *path, Byte_range_ptr const &dst) override
		{
			if (_matches(path))
				return Follow_error::NO_SYMLINK;

			return _fs._dot_dir_fs.follow(_fs._data_file_path().string(), dst);
		}
	};

	Device_file_system _device_fs { *this };

	Dir_file_system _dot_dir_fs { _env, *this, Dir_file_system::Name(".", _name) };

	Instance::Attempt create(Vfs::Env &, Parent_fs &, Node const &node) override
	{
		if (_device_fs .matches(node)) return { *this, { _device_fs  } };
		if (_dot_dir_fs.matches(node)) return { *this, { _dot_dir_fs } };

		return Error::DENIED;
	}

	void _free(Instance &) override { };

	File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &node)
	:
		Union_file_system(env, parent_fs, Ident { { "device ", name(node) } }),
		_env(env), _name(name(node))
	{ }

	~File_system()
	{
		_dot_dir_fs.update(Node(), *this);
		Union_file_system::update(Node(), *this);
	}

	Progress update(Node const &node, Vfs::File_system::Factory &global_factory) override
	{
		using Factory = Vfs::File_system::Factory;

		struct Both_factories : Factory
		{
			File_system &_fs;
			Factory &_global;

			Instance::Attempt create(Vfs::Env &env, Parent_fs &parent_fs, Node const &node) override
			{
				if (_fs._device_fs .matches(node)) return { _fs, { _fs._device_fs  } };
				if (_fs._dot_dir_fs.matches(node)) return { _fs, { _fs._dot_dir_fs } };

				return _global.create(env, parent_fs, node);
			}

			void _free(Instance &) override { /* never called */ };

			Both_factories(File_system &fs, Factory &g) : _fs(fs), _global(g) { }

		} both_factories { *this, global_factory };

		Generated_node config(_env.alloc(), 1024, "compound", [&] (Generator &g) {
			if (_name.length() > 1) {
				g.named_node("device", _name);
				g.named_node("dir", Dir_file_system::Name(".", _name), [&] {
					g.append_node_content(node); });
			}
		});

		Progress result { };
		config.node.with_result(
			[&] (Node const &node) {
				result = Union_file_system::update(node, both_factories); },
			[&] (Buffer_error) {
				warning("vfs_device: config '", _name, "' exceeds max capacity"); });

		return result;
	}

	void resume_after_update() override
	{
		/*
		 * Scan for data file within the dot directory. Use "data" if present.
		 * Otherwise, pick the first directory entry.
		 */

		_data_file_name = { };

		if (_dot_dir_fs.dir_entry_exists(_dot_dir_path("data").string()))
			_data_file_name = "data";

		if (_data_file_name.length() <= 1)
			_dot_dir_fs.opendir(_dot_dir_path("").string(), _env.alloc()).with_result(
				[&] (Vfs::Dir_channel &ch) {
					using Error = Vfs::Dir_channel::Read_dir_error;
					Dirent dirents[1] { };
					Vfs::Dir_channel::Read_dir_result result = Error::RETRY;
					for (;;) {
						result = ch.read_dir({ }, { dirents, 1 });
						if (result != Error::RETRY) break;
						_env.io().commit_and_wait();
					}
					result.with_result(
						[&] (size_t) { _data_file_name = Cstring(dirents[0].name.buf); },
						[&] (Error)  { });
					ch.destruct();
				},
				[&] (Vfs::Opendir_error) { });

		if (_data_file_name.length() <= 1)
			warning("vfs_device: missing data file for device '", _name, "'");

		Stat stat { };
		(void)_dot_dir_fs.stat(_data_file_path().string(), stat);
		_rwx = stat.rwx;
	}

	static constexpr auto BUILTIN_FS_TYPE = "device";
};

#endif /* _VFS__DEVICE_FILE_SYSTEM_H_ */
