/*
 * \brief  File system stress tester
 * \author Emery Hemingway
 * \date   2015-08-29
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/*
 * This test populates the VFS as follows:
 *
 * A directory is created at root for each thread with
 * sequential names. For each of these directories, a
 * subtree is generated until the maximum path depth
 * is reached at each branch. The subtree is layed out
 * like this:
 *
 * a
 * |\
 * a b
 * |\ \
 * a b b
 * |\ \ \
 * a b b b
 * |\ \ \ \
 * . . . . .
 *
 */

/* Genode includes */
#include <vfs/root.h>
#include <timer_session/connection.h>
#include <base/heap.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/exception.h>

using namespace Genode;


class Guard
{
	private:

		/*
		 * Noncopyable
		 */
		Guard(Guard const &);
		Guard &operator = (Guard const &);

		Genode::Vfs::File_channel * const _file_channel;

	public:

		Guard(Genode::Vfs::File_channel *file_channel) : _file_channel(file_channel) { }

		~Guard()
		{
			if (_file_channel)
				_file_channel->destruct();
		}
};


inline void assert_open(Vfs::Open_error e)
{
	switch (e) {
	case Vfs::Open_error::RETRY:       error("Open_error::RETRY");       break;
	case Vfs::Open_error::DENIED:      error("Open_error::DENIED");      break;
	case Vfs::Open_error::OUT_OF_RAM:  error("Open_error::OUT_OF_RAM");  break;
	case Vfs::Open_error::OUT_OF_CAPS: error("Open_error::OUT_OF_CAPS"); break;
	}
	throw Exception();
}

inline void assert_mkdir(Vfs::Mkdir_result r)
{
	using Result = Vfs::Mkdir_result;
	switch (r) {
	case Result::CREATED:
	case Result::UPDATED: return;
	case Result::DENIED:  error("Mkdir_result::DENIED"); break;
	case Result::RETRY:   error("Mkdir_result::RETRY");  break;
	}
	throw Exception();
}

inline void assert_write(Vfs::File_channel::Write_result r)
{
	r.with_error([&] (Vfs::File_channel::Write_error e) {
		switch (e) {
		case Vfs::File_channel::Write_error::RETRY:  error("Vfs::Write_error::RETRY");  break;
		case Vfs::File_channel::Write_error::DENIED: error("Vfs::Write_error::DENIED"); break;
		}
		throw Exception();
	});
}

inline void assert_read(Vfs::File_channel::Read_result r)
{
	r.with_error([&] (Vfs::File_channel::Read_error e) {
		switch (e) {
		case Vfs::File_channel::Read_error::RETRY:  error("Read_error::RETRY");  break;
		case Vfs::File_channel::Read_error::DENIED: error("Read_error::DENIED"); break;
		}
		throw Exception();
	});
}

inline void assert_unlink(Vfs::Unlink_result r)
{
	switch (r) {
	case Vfs::Unlink_result::OK:     return;
	case Vfs::Unlink_result::DENIED: error("Unlink_result::DENIED"); break;
	}
	throw Exception();
}

static int MAX_DEPTH;

using Path = Genode::Path<Vfs::MAX_PATH_LEN>;


struct Stress_test
{
	::Path             path;
	Vfs::file_size     count;
	Vfs::File_system  &vfs;
	Genode::Allocator &alloc;

	Stress_test(Vfs::File_system &vfs, Genode::Allocator &alloc, char const *parent)
	: path(parent), count(0), vfs(vfs), alloc(alloc) { }
};


void static mkdir(Vfs::File_system &vfs, Vfs::Env::Io &io, const char *path)
{
	Vfs::Mkdir_result mkdir_result;
	for (;;) {
		mkdir_result = vfs.mkdir(path, { });
		if (mkdir_result != Vfs::Mkdir_result::RETRY) break;
		io.commit_and_wait();
	}
	assert_mkdir(mkdir_result);
}


struct Mkdir_test : public Stress_test
{
	Vfs::Env::Io &_io;

	void mkdir_b(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		path.append("/b");
		mkdir(vfs, _io, path.base());
		++count;
		mkdir_b(depth);
	}

	void mkdir_a(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		size_t path_len = strlen(path.base());

		path.append("/b");
		mkdir(vfs, _io, path.base());
		++count;
		mkdir_b(depth);

		path.base()[path_len] = '\0';

		path.append("/a");
		mkdir(vfs, _io, path.base());
		++count;
		mkdir_a(depth);
	}

	Mkdir_test(Vfs::File_system &vfs, Vfs::Env::Io &io, Genode::Allocator &alloc,
	           char const *parent)
	:
		Stress_test(vfs, alloc, parent), _io(io)
	{
		try { mkdir_a(1); } catch (...) {
			error("failed at '", path, "' after ", count, " directories");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};


struct Populate_test : public Stress_test
{
	void populate(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		using namespace Vfs;

		size_t path_len = 1+strlen(path.base());
		char dir_type = *(path.base()+(path_len-2));

		path.append("/c");
		{
			File_channel *file_channel = nullptr;
			vfs.open(path.base(), { .writeable = true, .create = true }, alloc).with_result(
				[&] (Vfs::File_channel &c) { file_channel = &c; },
				[&] (Vfs::Open_error e)    { assert_open(e); });

			Guard guard(file_channel);
			++count;
		}

		switch (dir_type) {
		case 'a':
			path.base()[path_len] = '\0';
			path.append("a");
			populate(depth);
			[[fallthrough]];

		case 'b':
			path.base()[path_len] = '\0';
			path.append("b");
			populate(depth);
			return;

		default:
			error("bad directory '", Char(dir_type), "' at the end of '", path, "'");
			throw Exception();
		}
	}

	Populate_test(Vfs::File_system &vfs, Genode::Allocator &alloc, char const *parent)
	: Stress_test(vfs, alloc, parent)
	{
		::Path start_path(path.base());
		try {
			path.append("/a");
			populate(1);

			path.import(start_path.base());
			path.append("/b");
			populate(1);
		} catch (...) {
			error("failed at '", path, "' after ", count, " files");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};


struct Write_test : public Stress_test
{
	Vfs::Env::Io &_io;

	void write(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		size_t path_len = 1+strlen(path.base());
		char dir_type = *(path.base()+(path_len-2));

		using namespace Vfs;

		path.append("/c");
		{
			File_channel *file_channel = nullptr;
			vfs.open(path.base(), { .writeable = true, .create = false }, alloc).with_result(
				[&] (Vfs::File_channel &c) { file_channel = &c; },
				[&] (Vfs::Open_error e)    { assert_open(e); });

			Guard guard(file_channel);

			File_channel::Write_result r = file_channel->write({ }, Const_byte_range_ptr(path.base(), path_len));
			assert_write(r);

			while (file_channel->sync() == Vfs::Sync_result::RETRY)
				_io.commit_and_wait();

			count += r.convert<size_t>([&] (size_t n)                { return n; },
			                           [&] (File_channel::Write_error) { return 0ul; });
		}

		switch (dir_type) {
		case 'a':
			path.base()[path_len] = '\0';
			path.append("a");
			write(depth);
			[[fallthrough]];

		case 'b':
			path.base()[path_len] = '\0';
			path.append("b");
			write(depth);
			return;

		default:
			error("bad directory ", Char(dir_type), " at the end of '", path, "'");
			throw Exception();
		}
	}

	Write_test(Vfs::File_system &vfs, Vfs::Env::Io &io, Genode::Allocator &alloc,
	           char const *parent)
	:
		Stress_test(vfs, alloc, parent), _io(io)
	{
		size_t path_len = strlen(path.base());
		try {
			path.append("/a");
			write(1);

			path.base()[path_len] = '\0';
			path.append("/b");
			write(1);
		} catch (...) {
			error("failed at ",path," after writing ",count," bytes");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};


struct Read_test : public Stress_test
{
	Vfs::Env::Io &_io;

	void read(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		size_t path_len = 1+strlen(path.base());
		char dir_type = *(path.base()+(path_len-2));

		using namespace Vfs;

		path.append("/c");
		{
			File_channel *file_channel = nullptr;
			vfs.open(path.base(), { .writeable = false, .create = false }, alloc).with_result(
				[&] (Vfs::File_channel &c) { file_channel = &c; },
				[&] (Vfs::Open_error e)    { assert_open(e); });

			Guard guard(file_channel);

			char tmp[MAX_PATH_LEN];

			Vfs::File_channel::Read_result read_result = Vfs::File_channel::Read_error::DENIED;

			Byte_range_ptr const dst { tmp, sizeof(tmp) };

			for (;;) {
				read_result = file_channel->read({ }, dst);
				if (read_result != Vfs::File_channel::Read_error::RETRY)
					break;
				_io.commit_and_wait();
			}

			assert_read(read_result);

			read_result.with_result(
				[&] (size_t n) {
					if (strcmp(path.base(), tmp, (size_t)n))
						error("read returned bad data");
					count += n;
				},
				[&] (Vfs::File_channel::Read_error e) {
					switch (e) {
					case Vfs::File_channel::Read_error::RETRY:  error("Read_error::RETRY");  break;
					case Vfs::File_channel::Read_error::DENIED: error("Read_error::DENIED"); break;
					}
					throw Exception();
				});
		}

		switch (dir_type) {
		case 'a':
			path.base()[path_len] = '\0';
			path.append("a");
			read(depth);
			[[fallthrough]];

		case 'b':
			path.base()[path_len] = '\0';
			path.append("/b");
			read(depth);
			return;

		default:
			error("bad directory ", Char(dir_type), " at the end of '", path, "'");
			throw Exception();
		}
	}

	Read_test(Vfs::File_system &vfs, Vfs::Env::Io &io, Genode::Allocator &alloc,
	          char const *parent)
	:
		Stress_test(vfs, alloc, parent), _io(io)
	{
		size_t path_len = strlen(path.base());
		try {
			path.append("/a");
			read(1);

			path.base()[path_len] = '\0';
			path.append("/b");
			read(1);
		} catch (...) {
			error("failed at ",path," after reading ",count," bytes");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};


struct Unlink_test : public Stress_test
{
	Vfs::Env::Io &_io;

	void empty_dir(char const *path)
	{
		::Path subpath(path);
		subpath.append("/");

		vfs.opendir(path, alloc).with_result(
			[&] (Vfs::Dir_channel &channel) {

				Vfs::Dirent dirent { };
				for (unsigned i = vfs.num_dirent(path); i;) {
					--i;

					Vfs::Dir_channel::Read_dir_result result = Vfs::Dir_channel::Read_dir_error::DENIED;

					for (;;) {
						result = channel.read_dir({ i }, { &dirent, 1 });
						if (result != Vfs::Dir_channel::Read_dir_error::RETRY)
							break;
						_io.commit_and_wait();
					}
					if (result.failed()) {
						error("read of dir entry failed");
						throw Exception();
					}

					subpath.append(dirent.name.buf);
					switch (dirent.type) {

					case Vfs::Dirent_type::DIRECTORY:
						empty_dir(subpath.base());
						[[fallthrough]];

					default:
						try {
							assert_unlink(vfs.unlink(subpath.base()));
							++count;
						} catch (...) {
							error("unlink ", subpath," failed");
							throw;
						}
						subpath.strip_last_element();
					}
				}

				channel.destruct();
			},
			[&] (Vfs::Opendir_error e) {
				using Error = Vfs::Opendir_error;
				switch (e) {
				case Error::DENIED:      error("Opendir_error::DENIED");      break;
				case Error::RETRY:       error("Opendir_error::RETRY");       break;
				case Error::OUT_OF_RAM:  error("Opendir_error::OUT_OF_RAM");  break;
				case Error::OUT_OF_CAPS: error("Opendir_error::OUT_OF_CAPS"); break;
				}
				throw Exception();
			});
	}

	Unlink_test(Vfs::File_system &vfs, Vfs::Env::Io &io, Genode::Allocator &alloc,
	            char const *parent)
	:
		Stress_test(vfs, alloc, parent), _io(io)
	{
		using Result = Vfs::Unlink_result;
		try {
			Result r = vfs.unlink(path.base());
			switch (r) {
			case Result::DENIED:
				log("recursive unlink not supported");
				empty_dir(path.base());
				r = vfs.unlink(path.base());
				[[fallthrough]];

			case Result::OK:
				log("recursive unlink supported");
				++count;
				return;

			default: break;
			}
			assert_unlink(r);
		} catch (...) {
			error("unlink ",path," failed");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};

void die(Genode::Env &env, int code) { env.parent().exit(code); }

void Component::construct(Genode::Env &env)
{
	enum { ROOT_TREE_COUNT = 6 };

	Genode::Heap heap(env.ram(), env.rm());

	Vfs::Root vfs_root { env, heap };

	Attached_rom_dataspace config_rom(env, "config");

	config_rom.node().with_sub_node("vfs",
		[&] (Node const &config) { vfs_root.apply_config(config); },
		[&]                      { error("VFS not configured"); });

	String<Vfs::MAX_PATH_LEN> path { };

	MAX_DEPTH = config_rom.node().attribute_value("depth", 16U);

	uint64_t elapsed_ms;
	Timer::Connection timer(env);

	/* populate the directory file system at / */
	vfs_root.fs().num_dirent("/");

	auto used_ram_bytes = [&] { return env.pd().stats().ram.used.value; };

	size_t initial_consumption = used_ram_bytes();

	/**************************
	 ** Generate directories **
	 **************************/
	{
		Vfs::file_size count = 0;
		log("generating directory surface...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			mkdir(vfs_root.fs(), vfs_root.io(), path.string());
			Mkdir_test test(vfs_root.fs(), vfs_root.io(), heap, path.string());
			count += test.wait();
		}
		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		if (count > 0)
			log("created ",count," empty directories, ",
			    (elapsed_ms*1000)/count,"μs/op , ",
			    used_ram_bytes()/1024,"KiB consumed");
	}


	/********************
	 ** Generate files **
	 ********************/
	{
		Vfs::file_size count = 0;
		log("generating files...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			Populate_test test(vfs_root.fs(), heap, path.string());
			count += test.wait();
		}

		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		if (count > 0)
			log("created ",count," empty files, ",
			    (elapsed_ms*1000)/count,"μs/op, ",
			    used_ram_bytes()/1024,"KiB consumed");
	}


	/*****************
	 ** Write files **
	 *****************/

	if (!config_rom.node().attribute_value("write", true)) {
		elapsed_ms = timer.elapsed_ms();
		log("total: ",elapsed_ms,"ms, ",used_ram_bytes()/1024,"K consumed");
		return die(env, 0);
	}
	{
		Vfs::file_size count = 0;
		log("writing files...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			Write_test test(vfs_root.fs(), vfs_root.io(), heap, path.string());
			count += test.wait();

		}

		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		if (elapsed_ms > 0)
			log("wrote ",count," bytes, ",
			    count/elapsed_ms,"kB/s, ",
			    used_ram_bytes()/1024,"KiB consumed");
		else
			log("wrote ",count," bytes, ",
			    used_ram_bytes()/1024,"KiB consumed");
	}


	/*****************
	 ** Read files **
	 *****************/

	if (!config_rom.node().attribute_value("read", true)) {
		elapsed_ms = timer.elapsed_ms();

		log("total: ",elapsed_ms,"ms, ",used_ram_bytes()/1024,"KiB consumed");
		return die(env, 0);
	}
	{
		Vfs::file_size count = 0;
		log("reading files...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			Read_test test(vfs_root.fs(), vfs_root.io(), heap, path.string());
			count += test.wait();
		}

		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		if (elapsed_ms > 0)
			log("read ",count," bytes, ",
			    count/elapsed_ms,"kB/s, ",
			    used_ram_bytes()/1024,"KiB consumed");
		else
			log("read ",count," bytes, ",
			    used_ram_bytes()/1024,"KiB consumed");
	}


	/******************
	 ** Unlink files **
	 ******************/

	if (!config_rom.node().attribute_value("unlink", true)) {
		elapsed_ms = timer.elapsed_ms();
		log("total: ",elapsed_ms,"ms, ",used_ram_bytes()/1024,"KiB consumed");
		return die(env, 0);

	}
	{
		Vfs::file_size count = 0;

		log("unlink files...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			Unlink_test test(vfs_root.fs(), vfs_root.io(), heap, path.string());
			count += test.wait();

		}

		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		log("unlinked ",count," files in ",elapsed_ms,"ms, ",
		    used_ram_bytes()/1024,"KiB consumed");
	}

	log("total: ",timer.elapsed_ms(),"ms, ",
	    used_ram_bytes()/1024,"KiB consumed");

	size_t outstanding = used_ram_bytes() - initial_consumption;
	if (outstanding) {
		if (outstanding < 1024)
			error(outstanding, "B not freed after unlink and sync!");
		else
			error(outstanding/1024,"KiB not freed after unlink and sync!");
	}

	die(env, 0);
}
