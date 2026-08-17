 /*
 * \brief  Front-end API for accessing a component-local virtual file system
 * \author Norman Feske
 * \date   2017-07-04
 */

/*
 * Copyright (C) 2017-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__OS__VFS_H_
#define _INCLUDE__OS__VFS_H_

/* Genode includes */
#include <base/env.h>
#include <base/allocator.h>
#include <vfs/simple_env.h>
#include <vfs/dir_file_system.h>
#include <vfs/file_system_factory.h>
#include <vfs/file_handle.h>

namespace Genode {
	struct Directory;
	struct Root_directory;
	struct File;
	class  Readonly_file;
	class  File_content;
	class  Writeable_file;
	class  Append_file;
	class  New_file;
	class  Watcher;
	namespace Io {
		struct Watch_handler_base;
		template <typename>
		class Watch_handler;
	}
	template <typename>
	class  Watch_handler;
	void with_raw_file_content (Readonly_file const &, Byte_range_ptr const &, auto const &);
	void with_xml_file_content (Readonly_file const &, Byte_range_ptr const &, auto const &);
	void with_node_file_content(Readonly_file const &, Byte_range_ptr const &, auto const &);
}


struct Genode::Directory : Noncopyable, Interface
{
	public:

		struct Open_failed     : Exception { };
		struct Read_dir_failed : Exception { };

		class Entry
		{
			private:

				Vfs::Directory_service::Dirent _dirent { };

				friend class Directory;

				Entry() { }

				using Dirent_type = Vfs::Directory_service::Dirent_type;

			public:

				void print(Output &out) const
				{
					using Genode::print;
					using Vfs::Directory_service;

					print(out, _dirent.name.buf, " (");
					switch (_dirent.type) {
					case Dirent_type::TRANSACTIONAL_FILE: print(out, "file");    break;
					case Dirent_type::CONTINUOUS_FILE:    print(out, "file");    break;
					case Dirent_type::DIRECTORY:          print(out, "dir");     break;
					case Dirent_type::SYMLINK:            print(out, "symlink"); break;
					default:                              print(out, "other");   break;
					}
					print(out, ")");
				}

				using Name = String<Vfs::Directory_service::Dirent::Name::MAX_LEN>;

				Name name() const { return Name(Cstring(_dirent.name.buf)); }

				Vfs::Directory_service::Dirent_type type() const { return _dirent.type; }

				bool dir() const { return _dirent.type == Dirent_type::DIRECTORY; }

				Vfs::Node_rwx rwx() const { return _dirent.rwx; }
		};

		enum { MAX_PATH_LEN = 256 };

		using Path = String<MAX_PATH_LEN>;

		static Path join(Path const &x, Path const &y)
		{
			char const *p = y.string();
			while (*p == '/') ++p;

			if (x == "/")
				return Path("/", p);

			return Path(x, "/", p);
		}

	private:

		/*
		 * Noncopyable
		 */
		Directory(Directory const &);
		Directory &operator = (Directory const &);

		Path const _path;

		Vfs::Env         &_vfs_env;
		Vfs::File_system &_fs    = _vfs_env.fs();
		Vfs::Env::Io     &_io    = _vfs_env.io();
		Allocator        &_alloc = _vfs_env.alloc();

		Vfs::Dir_handle _handle;

		friend class Readonly_file;
		friend class Root_directory;
		friend class Io::Watch_handler_base;
		friend class Writeable_file;
		friend class Append_file;
		friend class New_file;

		/*
		 * Operations such as 'file_size' that are expected to be 'const' at
		 * the API level, do internally require I/O with the outside world,
		 * with involves non-const access to the VFS. This helper allows a
		 * 'const' method to perform I/O at the VFS.
		 */
		Vfs::File_system &_nonconst_fs() const
		{
			return const_cast<Vfs::File_system &>(_fs);
		}

		Vfs::Directory_service::Stat_result _stat(Path const &rel_path,
		                                          Vfs::Directory_service::Stat &out) const
		{
			if (rel_path == "")
				return _nonconst_fs().stat(_path.string(), out);
			return _nonconst_fs().stat(join(_path, rel_path).string(), out);
		}

		auto _with_entry(unsigned i,
		                 auto const &fn,
		                 auto const &missing_fn) -> decltype(missing_fn())
		{
			Entry entry;

			Vfs::Read_result read_result = 0;
			for (;;) {
				Byte_range_ptr const dst { (char*)&entry._dirent,
				                            sizeof(entry._dirent) };
				Vfs::At const at { .pos = i*sizeof(entry._dirent) };

				read_result = _handle.read(at, dst);
				if (read_result != Vfs::Read_error::RETRY)
					break;

				_io.commit_and_wait();
			}

			bool const ok = read_result.convert<bool>(
				[&] (size_t num_bytes) {
					if ((num_bytes > 0) && (num_bytes < sizeof(entry._dirent)))
						warning("failed to access dir entry ", i, " of '", _path, "'");

					return (num_bytes == sizeof(entry._dirent))
					    && (entry._dirent.type != Vfs::Directory_service::Dirent_type::END);
				},
				[&] (Vfs::Read_error) { return false; });

			if (ok)
				return fn(static_cast<Entry const &>(entry));

			return missing_fn();
		}

	public:

		struct Nonexistent_file : Exception { };

		/**
		 * Constructor used by 'Root_directory'
		 */
		Directory(Vfs::Env &vfs_env)
		:
			_path(""), _vfs_env(vfs_env),
			_handle(_vfs_env.dir_handles(), _vfs_env.fs(), _alloc, "/")
		{ }

		/**
		 * Open sub directory
		 */
		Directory(Directory const &other, Path const &rel_path)
		:
			_path(join(other._path, rel_path)), _vfs_env(other._vfs_env),
			_handle(_vfs_env.dir_handles(), _vfs_env.fs(), _alloc, _path)
		{ }

		bool exists() const { return directory_exists(""); }

		void for_each_entry(auto const &fn)
		{
			for (unsigned i = 0;; i++) {

				bool const ok = _with_entry(i,
					[&] (Entry const &e) { fn(e); return true; },
					[&] () -> bool       {        return false; });

				if (!ok)
					break;
			}
		}

		auto with_first_entry(auto const &fn,
		                      auto const &missing_fn) -> decltype(missing_fn())
		{
			return _with_entry(0, fn, missing_fn);
		}

		void for_each_entry(auto const &fn) const
		{
			auto const_fn = [&] (Entry const &e) { fn(e); };
			const_cast<Directory &>(*this).for_each_entry(const_fn);
		}

		bool file_exists(Path const &rel_path) const
		{
			Vfs::Directory_service::Stat stat { };

			if (_stat(rel_path, stat) != Vfs::Directory_service::STAT_OK)
				return false;

			return stat.type == Vfs::Node_type::TRANSACTIONAL_FILE
			    || stat.type == Vfs::Node_type::CONTINUOUS_FILE;
		}

		bool directory_exists(Path const &rel_path) const
		{
			Vfs::Directory_service::Stat stat { };

			if (_stat(rel_path, stat) != Vfs::Directory_service::STAT_OK)
				return false;

			return stat.type == Vfs::Node_type::DIRECTORY;
		}

		bool symlink_exists(Path const &rel_path) const
		{
			Vfs::Directory_service::Stat stat { };

			if (_stat(rel_path, stat) != Vfs::Directory_service::STAT_OK)
				return false;

			return stat.type == Vfs::Node_type::SYMLINK;
		}

		bool entry_exists(Path const &rel_path) const
		{
			Vfs::Directory_service::Stat stat { };
			return _stat(rel_path, stat) == Vfs::Directory_service::STAT_OK;
		}

		/**
		 * Return size of file at specified directory-relative path
		 *
		 * \throw Nonexistent_file  file at path does not exist or
		 *                          the access to the file is denied
		 *
		 */
		Vfs::file_size file_size(Path const &rel_path) const
		{
			Vfs::Directory_service::Stat stat { };

			if (_stat(rel_path, stat) != Vfs::Directory_service::STAT_OK)
				throw Nonexistent_file();

			if (stat.type == Vfs::Node_type::TRANSACTIONAL_FILE
			 || stat.type == Vfs::Node_type::CONTINUOUS_FILE)
				return stat.size;

			throw Nonexistent_file();
		}

		/**
		 * Return symlink content at specified directory-relative path
		 *
		 * \throw Nonexistent_file  symlink at path does not exist or
		 *                          access is denied
		 *
		 */
		Path read_symlink(Path const &rel_path) const
		{
			using namespace Vfs;
			Vfs_handle *link_handle;

			auto open_res = _nonconst_fs().openlink(
				join(_path, rel_path).string(),
				false, &link_handle, _alloc);

			if (open_res != Directory_service::OPENLINK_OK)
				throw Nonexistent_file();

			Vfs_handle::Guard guard(link_handle);

			char buf[MAX_PATH_LEN];

			Vfs_handle::Read_result result = Vfs_handle::Read_error::DENIED;
			for (;;) {
				result = link_handle->read({ }, Byte_range_ptr(buf, sizeof(buf) - 1));
				if (result != Vfs_handle::Read_error::RETRY)
					break;

				_io.commit_and_wait();
			};

			return result.convert<Path>(
				[&] (size_t num_bytes) { return Path(Genode::Cstring(buf, num_bytes)); },
				[&] (Vfs_handle::Read_error) -> Path  { throw Nonexistent_file(); });
		}

		/**
		 * Attempt to create symlink
		 *
		 * This operation may fail. Its success can be checked by calling
		 * 'symlink_exists'.
		 */
		void create_symlink(Path const &rel_path, Path const &target)
		{
			using namespace Vfs;
			Vfs_handle *link_handle;

			auto openlink_result = _nonconst_fs().openlink(
				join(_path, rel_path).string(),
				true, &link_handle, _alloc);

			using Openlink_result = Directory_service::Openlink_result;

			if (openlink_result == Openlink_result::OPENLINK_ERR_NODE_ALREADY_EXISTS)
				openlink_result = _fs.openlink(
					join(_path, rel_path).string(),
					false, &link_handle, _alloc);

			if (openlink_result != Openlink_result::OPENLINK_OK)
				return;

			Vfs_handle::Guard guard(link_handle);

			Const_byte_range_ptr const src { target.string(), target.length() };

			Vfs_handle::Write_result write_result = Vfs_handle::Write_error::DENIED;

			for (;;) {
				write_result = link_handle->write({ }, src);
				if (write_result != Vfs_handle::Write_error::RETRY)
					break;
				_io.commit_and_wait();
			}

			write_result.with_result([&] (size_t num_bytes) {
				if (num_bytes < src.num_bytes) {
					warning("failed to write complete symlink");
					unlink(rel_path);
				}
			}, [&] (Vfs_handle::Write_error) { });

			/* sync before the handle gets closed */
			while (link_handle->sync() == Sync_result::RETRY)
				_io.commit_and_wait();
		}

		void unlink(Path const &rel_path)
		{
			_fs.unlink(join(_path, rel_path).string());
		}

		void rename(Path const &from, Path const &to)
		{
			_fs.rename(join(_path, from).string(), join(_path, to).string());
		}

		/**
		 * Attempt to create sub directory
		 *
		 * This operation may fail. Its success can be checked by calling
		 * 'directory_exists'.
		 */
		void create_sub_directory(Path const &sub_path)
		{
			using namespace Genode;

			for (size_t sub_path_len = 0; ; sub_path_len++) {

				char const c = sub_path.string()[sub_path_len];

				bool const end_of_path = (c == 0);
				bool const end_of_elem = (c == '/');

				if (!end_of_elem && !end_of_path)
					continue;

				Path path = join(_path, Path(Cstring(sub_path.string(), sub_path_len)));

				if (!directory_exists(path)) {
					Vfs::Vfs_handle *handle_ptr = nullptr;
					(void)_fs.opendir(path.string(), true, &handle_ptr, _alloc);
					if (handle_ptr)
						handle_ptr->close();
				}

				if (end_of_path)
					break;

				/* skip '/' */
				sub_path_len++;
			}
		}
};


struct Genode::Root_directory : public Vfs::Simple_env,
                                public Directory
{
	Root_directory(Genode::Env &env, Allocator &alloc, Node const &config)
	:
		Vfs::Simple_env(env, alloc, config), Directory((Vfs::Simple_env&)*this)
	{ }
};


struct Genode::File : Noncopyable, Interface
{
	struct Open_failed : Exception { };

	struct Truncated_during_read : Exception { };

	using Path = Directory::Path;
};


class Genode::Readonly_file : public File
{
	private:

		/*
		 * Noncopyable
		 */
		Readonly_file(Readonly_file const &);
		Readonly_file &operator = (Readonly_file const &);

		Vfs::File_handle _handle;
		Vfs::Env::Io &_io;

		/**
		 * Strip off constness of 'Directory const &'
		 *
		 * Since the 'Readonly_file' API provides an abstraction over the
		 * low-level VFS operations, the intuitive meaning of 'const' is
		 * different between the 'Readonly_file' API and the VFS.
		 *
		 * At the VFS level, opening a file changes the internal state of the
		 * VFS. Hence the operation is non-const. However, the user of the
		 * 'Readonly_file' API expects the constness of a directory to
		 * correspond to whether the directory can be modified or not. In the
		 * case of instantiating a 'Readonly_file', one would expect that a
		 * 'Directory const &' would suffice. The fact that - under the hood -
		 * the 'Readonly_file' has to perform the nonconst 'open' operation at
		 * the VFS is of not of interest.
		 */
		static Directory &_mutable(Directory const &dir)
		{
			return const_cast<Directory &>(dir);
		}

	public:

		/**
		 * Constructor
		 *
		 * \throw File::Open_failed
		 */
		Readonly_file(Directory const &dir, Path const &rel_path)
		:
			_handle(_mutable(dir)._vfs_env.file_handles(),
			        _mutable(dir)._fs, _mutable(dir)._alloc,
			        { .path      = Directory::join(dir._path, rel_path),
			          .writeable = false }),
			_io(_mutable(dir)._io)
		{ }

		using At = Vfs::At;

		/**
		 * Read file content starting at 'at' into byte buffer 'range'
		 */
		size_t read(At const at, Byte_range_ptr const &range) const
		{
			Vfs::File_handle &handle = const_cast<Vfs::File_handle &>(_handle);

			size_t total = 0;
			for (;;) {

				Vfs::Read_result result = Vfs::Read_error::DENIED;
				for (;;) {

					Byte_range_ptr const partial_range { range.start     + total,
					                                     range.num_bytes - total };
					Vfs::At const partial_at { .pos = at.pos + total };

					result = handle.read(partial_at, partial_range);
					if (result != Vfs::Read_error::RETRY)
						break;

					_io.commit_and_wait();
				};

				/* byte count for this iteration */
				size_t const read_bytes = result.convert<size_t>(
					[&] (size_t n)        { return n; },
					[&] (Vfs::Read_error) { return 0ul; });

				if (read_bytes > range.num_bytes - total) {
					error("read beyond buffer size");
					break;
				}

				if (read_bytes == 0)
					break;

				total += size_t(read_bytes);
			}
			return total;
		}

		/*
		 * \deprecated  use 'Byte_range_ptr'
		 */
		size_t read(char *dst, size_t bytes) const __attribute__((deprecated))
		{
			return read(At{0}, Byte_range_ptr(dst, bytes));
		}

		/*
		 * \deprecated  use 'Byte_range_ptr'
		 */
		size_t read(At at, char *dst, size_t bytes) const __attribute__((deprecated))
		{
			return read(at, Byte_range_ptr(dst, bytes));
		}

		/**
		 * Read file content into byte buffer 'range'
		 */
		size_t read(Byte_range_ptr const &range) const
		{
			return read(At{0}, range);
		}
};


/**
 * Call functor 'fn' with the data pointer and size in bytes
 *
 * If the buffer has a size of zero, 'fn' is not called.
 *
 * \throw Truncated_during_read
 */
void Genode::with_raw_file_content(Readonly_file const &file,
                                   Byte_range_ptr const &range, auto const &fn)
{
	if (range.num_bytes == 0)
		return;

	if (file.read(range) != range.num_bytes)
		throw File::Truncated_during_read();

	fn(range.start, range.num_bytes);
}


/**
 * Call functor 'fn' with content as 'Xml_node' argument
 *
 * If the file does not contain valid XML, 'fn' is called with an
 * '<empty/>' node as argument.
 */
void Genode::with_xml_file_content(Readonly_file const &file,
                                   Byte_range_ptr const &range, auto const &fn)
{
	with_raw_file_content(file, range, [&] (char const *ptr, size_t num_bytes) {

		try {
			fn(Xml_node(ptr, num_bytes));
			return;
		}
		catch (Xml_node::Invalid_syntax) { }

		fn(Xml_node("<empty/>"));
	});
}


void Genode::with_node_file_content(Readonly_file const &file,
                                    Byte_range_ptr const &range, auto const &fn)
{
	with_raw_file_content(file, range, [&] (char const *ptr, size_t num_bytes) {
		fn(Node(Const_byte_range_ptr(ptr, num_bytes))); });
}


class Genode::File_content
{
	public:

		struct Limit { size_t value; };

	private:

		class Buffer
		{
			private:

				/*
				 * Noncopyable
				 */
				Buffer(Buffer const &);
				Buffer &operator = (Buffer const &);

			public:

				Allocator   &alloc;
				size_t const size;
				char * const ptr = size ? (char *)alloc.alloc(size) : nullptr;

				Buffer(Allocator &alloc, size_t size) : alloc(alloc), size(size) { }
				~Buffer() { if (ptr) alloc.free(ptr, size); }

		} _buffer;

		static size_t _checked_file_size(Vfs::file_size file_size, Limit limit)
		{
			if (file_size <= limit.value)
				return size_t(file_size);

			throw Truncated_during_read();
		}

	public:

		using Nonexistent_file      = Directory::Nonexistent_file;
		using Truncated_during_read = File::Truncated_during_read;
		using Path                  = Directory::Path;

		/**
		 * Constructor
		 *
		 * \throw Nonexistent_file
		 * \throw Truncated_during_read  number of readable bytes differs
		 *                               from file status information
		 */
		File_content(Allocator &alloc, Directory const &dir, Path const &rel_path,
		             Limit limit)
		:
			_buffer(alloc, _checked_file_size(dir.file_size(rel_path), limit))
		{
			/* read the file content into the buffer */
			with_raw_file_content(Readonly_file(dir, rel_path),
			                      Byte_range_ptr(_buffer.ptr, _buffer.size),
			                      [] (char const*, size_t) { });
		}

		/**
		 * Call functor 'fn' with content as 'Xml_node' argument
		 *
		 * If the file does not contain valid XML, 'fn' is called with an
		 * '<empty/>' node as argument.
		 */
		void xml(auto const &fn) const
		{
			try {
				if (_buffer.size) {
					fn(Xml_node(_buffer.ptr, _buffer.size));
					return;
				}
			}
			catch (Xml_node::Invalid_syntax) { }

			fn(Xml_node("<empty/>"));
		}

		void node(auto const &fn) const
		{
			fn(Node(Const_byte_range_ptr(_buffer.ptr, _buffer.size)));
		}

		/**
		 * Call functor 'fn' with each line of the file as argument
		 *
		 * \param STRING  string type used for the line
		 */
		template <typename STRING>
		void for_each_line(auto const &fn) const
		{
			char const *src           = _buffer.ptr;
			char const *curr_line     = src;
			size_t      curr_line_len = 0;

			for (size_t n = 0; ; n++) {

				char const c = (n == _buffer.size) ? 0 : *src++;
				bool const end_of_data = (c == 0);
				bool const end_of_line = (c == '\n');

				if (!end_of_data && !end_of_line) {
					curr_line_len++;
					continue;
				}

				if (!end_of_data || curr_line_len > 0)
					fn(STRING(Cstring(curr_line, curr_line_len)));

				if (end_of_data)
					break;

				curr_line     = src;
				curr_line_len = 0;
			}
		}

		/**
		 * Call functor 'fn' with the data pointer and size in bytes
		 *
		 * If the buffer has a size of zero, 'fn' is not called.
		 */
		void bytes(auto const &fn) const
		{
			if (_buffer.size)
				fn((char const *)_buffer.ptr, _buffer.size);
		}
};


/**
 * Base class of `New_file` and `Append_file` providing open for write, sync,
 * and append functionality.
 */
class Genode::Writeable_file : Noncopyable
{
	public:

		struct Create_failed : Exception { };

		enum class Append_result { OK, WRITE_ERROR };

	private:

		bool const _compound_dir_created;

		static bool _create_compound_dir(Directory             &dir,
		                                 Directory::Path const &rel_path)
		{
			Genode::Path<Vfs::MAX_PATH_LEN> dir_path { rel_path };
			dir_path.strip_last_element();
			dir.create_sub_directory(dir_path.string());
			return true;
		}

	protected:

		Vfs::Env::Io &_io;

		Vfs::File_handle _handle;

		Append_result _append(Vfs::At &at, Const_byte_range_ptr const &src)
		{
			bool write_error = false;

			size_t remaining_bytes = src.num_bytes;

			char const * src_ptr = src.start;

			while (remaining_bytes > 0 && !write_error) {

				Const_byte_range_ptr const partial_src { src_ptr, remaining_bytes };

				Vfs::Write_result result = Vfs::Write_error::DENIED;
				for (;;) {
					result = _handle.write(at, partial_src);
					if (result != Vfs::Write_error::RETRY)
						break;
					_io.commit_and_wait();
				}

				result.with_result(
					[&] (size_t num_bytes) {
						num_bytes = min(remaining_bytes, num_bytes);
						remaining_bytes -= num_bytes;
						src_ptr         += num_bytes;
						at.pos          += num_bytes;
					},
					[&] (Vfs::Write_error) {
						write_error = true;
					});
			}
			return write_error ? Append_result::WRITE_ERROR
			                   : Append_result::OK;
		}

		Writeable_file(Directory &dir, Directory::Path const &path)
		:
			_compound_dir_created(_create_compound_dir(dir, path)),
			_io(dir._io),
			_handle(dir._vfs_env.file_handles(), dir._fs, dir._alloc,
			        { .path      = Directory::join(dir._path, path),
			          .writeable = true })
		{
			for (;;) {
				Vfs::File_handle::Attach_result const result = _handle.attach();
				if (result == Vfs::File_handle::Attach_error::RETRY) {
					_io.commit_and_wait();
					continue;
				}
				if (result.ok())
					break;
				throw Create_failed();
			}
		}

		~Writeable_file()
		{
			while (_handle.detach() == Vfs::File_handle::Detach_result::RETRY)
				_io.commit_and_wait();
		}
};


/**
 * Utility for appending data to an existing file via the Genode VFS library
 */
class Genode::Append_file : public Writeable_file
{
	private:

		Vfs::At _at { };

	public:

		/**
		 * Constructor
		 *
		 * \throw Create_failed
		 */
		Append_file(Directory &dir, Directory::Path const &path)
		:
			Writeable_file(dir, path)
		{
			Vfs::Directory_service::Stat stat { };
			if (dir._stat(path, stat) == Vfs::Directory_service::STAT_OK)
				_at.pos = stat.size;
		}

		Append_result append(Const_byte_range_ptr const &src) {
			return _append(_at, src); }

		Append_result append(char const *src, size_t size) {
			return _append(_at, Const_byte_range_ptr(src, size)); }
};


/**
 * Utility for writing data to a new file via the Genode VFS library
 */
class Genode::New_file : public Writeable_file
{
	private:

		Vfs::At _at { };

	public:

		using Writeable_file::Append_result;
		using Writeable_file::Create_failed;

		/**
		 * Constructor
		 *
		 * \throw Create_failed
		 */
		New_file(Directory &dir, Directory::Path const &path)
		:
			Writeable_file(dir, path)
		{ }

		~New_file()
		{
			/* the new file may be smaller than the previous version */
			while (_handle.resize(_at.pos) == Vfs::Resize_result::RETRY)
				_io.commit_and_wait();
		}

		Append_result append(Const_byte_range_ptr const &src) {
			return _append(_at, src); }

		Append_result append(char const *src, size_t size) {
			return _append(_at, Const_byte_range_ptr(src, size)); }
};


/**
 * Helper with access to 'Directory::_vfs_env' and 'Directory::_path'
 *
 * \noapi
 */
struct Genode::Io::Watch_handler_base : Vfs::Watch_handle::Handler
{
	Vfs::Watch_handle _handle;

	static Directory &_mutable(Directory const &dir)
	{
		return const_cast<Directory &>(dir);
	}

	Watch_handler_base(Directory const &dir, Directory::Path const &rel_path)
	:
		_handle(_mutable(dir)._vfs_env.watch_handles(),
		        _mutable(dir)._vfs_env.fs(),
		        Directory::join(dir._path, rel_path),
		        *this)
	{ }

	Vfs::Watch_result watch() { return _handle.watch(); }
};


/**
 * Watch handler that operates on I/O signal level
 */
template <typename T>
class Genode::Io::Watch_handler : private Io::Watch_handler_base
{
	private:

		T  &_obj;
		void (T::*_member) ();

		/**
		 * Vfs::Watch_handle::Handler interface
		 */
		void io_handle_watch() override { (_obj.*_member)(); }

	public:

		Watch_handler(Directory const &dir, Directory::Path const &rel_path,
		              T &obj, void (T::*member)())
		:
			Io::Watch_handler_base(dir, rel_path),
			_obj(obj), _member(member)
		{ }

		using Io::Watch_handler_base::watch;
};


/**
 * Watch handler that operates on application signal level
 *
 * If the watched file exists at construction time, the handler triggers
 * once immedialy.
 */
template <typename T>
class Genode::Watch_handler : public Signal_handler<Watch_handler<T>>
{
	private:

		using This = Watch_handler<T>;

		Io::Watch_handler<This> _io_watch_handler;

		void _io_handle_watch() { This::local_submit(); }

		T  &_obj;
		void (T::*_member) ();

		void _handle_signal() { (_obj.*_member)(); }

	public:

		Watch_handler(Genode::Entrypoint &ep, Directory const &dir,
		              Directory::Path const &rel_path,
		              T &obj, void (T::*member)())
		:
			Signal_handler<This>(ep, *this, &This::_handle_signal),
			_io_watch_handler(dir, rel_path, *this, &This::_io_handle_watch),
			_obj(obj), _member(member)
		{
			if (_io_watch_handler.watch().failed())
				warning("VFS unable to watch ", rel_path);

			/* deliver initial watch notification if watched dir entry exists */
			if (dir.entry_exists(rel_path))
				This::local_submit();
		}
};

#endif /* _INCLUDE__OS__VFS_H_ */
