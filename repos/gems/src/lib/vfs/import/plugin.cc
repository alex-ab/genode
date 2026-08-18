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
	/*
	 * XXX: would be a temporary heap but destructing a VFS is not supported
	 */
	Heap _heap;

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
		_heap(env.env().ram(), env.env().rm())
	{
		bool overwrite = config.attribute_value("overwrite", false);

		Root_directory src(env.env(), _heap, config);
		Directory      dst(env);

		_copy_dir(src, dst, Directory::Path(""), overwrite);
	}

	const char* type() override { return "import"; }

	Dataspace_capability dataspace(char const*) override { return { }; }

	void release(char const*, Dataspace_capability) override { }

	Open_result open(const char*, unsigned, Vfs::Vfs_handle**, Allocator&) override {
		return Open_result::OPEN_ERR_UNACCESSIBLE; }

	Opendir_result opendir(char const*, bool,
	                       Vfs_handle**, Allocator&) override {
		return OPENDIR_ERR_LOOKUP_FAILED; }

	void close(Vfs_handle*) override { }

	Stat_result stat(const char*, Directory_service::Stat&) override {
		return STAT_ERR_NO_ENTRY; }

	Unlink_result unlink(const char*) override { return UNLINK_ERR_NO_ENTRY; }

	Rename_result rename(const char*, const char*) override {
		return RENAME_ERR_NO_ENTRY; }

	unsigned num_dirent(const char*) override { return 0; }

	bool directory(char const*) override { return false; }

	bool dir_entry_exists(const char *) override { return false; }
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		Vfs::File_system *create(Vfs::Env &env, Vfs::Parent_fs &, Node const &config) override
		{
			return new (env.alloc()) Vfs_import::File_system(env, config);
		}
	};

	static Factory f;
	return &f;
}
