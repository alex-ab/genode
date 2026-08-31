/*
 * \brief  VFS content initialization/import plugin
 * \author Emery Hemingway
 * \author Norman Feske
 * \date   2018-07-05
 */

/*
 * Copyright (C) 2018-2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <os/vfs.h>
#include <base/heap.h>

namespace Vfs_import {

	using namespace Genode;
	using namespace Genode::Vfs;

	class File_system;
}


struct Vfs_import::File_system : Vfs::File_system
{
	Vfs::Env &_env;

	/*
	 * XXX: would be a temporary heap but destructing a VFS is not supported
	 */
	Heap _heap;

	Buffered_node _config;

	bool _done = false;

	static void _copy_file(Directory const &src, Directory &dst,
	                       Directory::Path const &path)
	{
		Readonly_file src_file(src, path);

		bool write_error = false;
		try {
			New_file dst_file(dst, path);

			char buf[4096];
			At at { };

			while (true) {

				size_t const num_bytes =
					src_file.read(at, Byte_range_ptr(buf, sizeof(buf)));

				if (!num_bytes) /* EOF */
					break;

				write_error = dst_file.append(buf, num_bytes) != New_file::Append_result::OK;
				if (write_error)
					break;

				at.pos  += num_bytes;
			}
		} catch (New_file::Create_failed) {
			warning("skipping import of file ", path, " (create failed)");
		}
		if (write_error) {
			warning("skipping import of file ", path, " (write failed)");
			dst.unlink(path);
		}
	}

	static void _copy_dir(Root_directory &src, Directory &dst,
	                      Directory::Path const &path, bool overwrite)
	{
		dst.create_sub_directory(path);

		Directory(src,path).for_each_entry([&] (Directory::Entry const &e) {
			auto entry_path = Directory::join(path, e.name());
			switch (e.type()) {
			case Dirent_type::TRANSACTIONAL_FILE:
			case Dirent_type::CONTINUOUS_FILE:
				if (dst.entry_exists(entry_path) && !overwrite)
					log("retaining ", entry_path, " instead of importing file");
				else
					_copy_file(src, dst, entry_path);
				return;
			case Dirent_type::DIRECTORY:
				_copy_dir(src, dst, entry_path, overwrite);
				return;
			case Dirent_type::SYMLINK:
				if (dst.entry_exists(entry_path) && !overwrite)
					log("retaining ", entry_path, " instead of importing symlink");
				else {
					try {
						dst.create_symlink(entry_path, src.read_symlink(entry_path));
					} catch (...) {
						warning("failed to import symlink ", entry_path);
					}
				}
				return;
			case Dirent_type::END:
				return;
			}
			warning("skipping import of ", e);
		});
	}

	File_system(Vfs::Env &env, Node const &config)
	:
		Vfs::File_system(Ident::from_node(config)),
		_env(env), _heap(env.env().ram(), env.env().rm()),
		_config(env.alloc(), config)
	{ }

	Progress update(Node const &, Factory &) override
	{
		/* respond to the initial update only */
		return _done ? STALLED : PROGRESSED;
	}

	void resume_after_update() override
	{
		if (_done) return;

		bool overwrite = _config.attribute_value("overwrite", false);

		Root_directory src(_env.env(), _heap, _config);
		Directory      dst(_env);

		_copy_dir(src, dst, Directory::Path(""), overwrite);

		_done = true;
	}
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		using Fs = Vfs_import::File_system;

		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &, Node const &config) override
		{
			return { *this, { *new (env.alloc()) Fs(env, config) } };
		}

		void _free(Instance &) override
		{
			warning("vfs_import fs cannot be freed");
		}
	};

	static Factory f;
	return &f;
}
