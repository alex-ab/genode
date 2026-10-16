/*
 * \brief  VFS pipe plugin
 * \author Emery Hemingway
 * \author Sid Hussmann
 * \date   2019-05-29
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 * Copyright (C) 2020 gapfruit AG
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <vfs/env.h>
#include <os/path.h>
#include <os/ring_buffer.h>
#include <base/registry.h>

namespace Vfs_pipe {

	using namespace Genode;
	using namespace Genode::Vfs;

	using Path = Path<MAX_PATH_LEN>;

	enum { PIPE_BUF_SIZE = 8192U };
	using Pipe_buffer = Ring_buffer<unsigned char, PIPE_BUF_SIZE+1>;

	struct File_channel;
	struct Dir_channel;

	using File_channel_fifo_element = Fifo_element<File_channel>;
	using File_channel_fifo         = Fifo<File_channel_fifo_element>;

	using File_channels = Registry<File_channel>;

	struct Pipe;
	using Pipe_space = Id_space<Pipe>;

	struct New_file_channel;

	class File_system;
	class Pipe_file_system;
	class Fifo_file_system;
}


struct Vfs_pipe::File_channel : Vfs::File_channel, private File_channels::Element
{
	Allocator &_alloc;

	Pipe &pipe;

	File_channel_fifo_element read_ready_elem { *this };

	bool const writer = Vfs::File_channel::writeable;

	File_channel(Allocator &alloc, Attr attr, File_channels &channels, Pipe &p)
	:
		Vfs::File_channel(attr), File_channels::Element(channels, *this),
		_alloc(alloc), pipe(p)
	{ }

	virtual ~File_channel();

	Write_result write(At, Const_byte_range_ptr const &) override;
	Read_result  read(At, Byte_range_ptr const &) override;

	Resize_result resize(file_size) override { return Resize_result::DENIED; }

	bool read_ready()  const override;
	bool write_ready() const override;
	void notify_read_ready() override;

	void destruct() override;
};


struct Vfs_pipe::Pipe
{
	Vfs::Env &_env;

	Pipe_space::Element space_elem;

	Pipe_buffer buffer { };

	File_channels file_channels { };

	File_channel_fifo read_ready_waiters { };

	unsigned num_writers = 0;
	bool waiting_for_writers = true;

	Io_signal_handler<Pipe> _read_notify_handler { _env.env().ep(), *this, &Pipe::notify_read };

	bool new_channel_active = true;

	Pipe(Vfs::Env &env, Pipe_space &space) : _env(env), space_elem(*this, space) { }

	~Pipe() = default;

	using Name = String<8>;
	Name name() const { return Name(space_elem.id().value); }

	void notify_read()
	{
		read_ready_waiters.dequeue_all([] (File_channel_fifo_element &elem) {
			elem.object().read_ready_response(); });
	}

	void submit_read_signal()
	{
		_read_notify_handler.local_submit();
	}

	void submit_write_signal() { _env.user().wakeup_vfs_user(); }

	unsigned num_file_channels() const
	{
		unsigned n = 0;
		file_channels.for_each([&] (File_channel const &) { n++; });
		return n;
	}

	void destruct() { destroy(_env.alloc(), this); }

	/**
	 * Detach a channel
	 */
	void remove(File_channel &c)
	{
		if (c.read_ready_elem.enqueued())
			read_ready_waiters.remove(c.read_ready_elem);
	}

	/**
	 * Open a write or read channel
	 */
	Open_result open(Path const &filename, Allocator &alloc)
	{
		if (filename == "/in") {

			if (0 == num_writers) {
				/* flush buffer */
				if (!buffer.empty())
					warning("flushing non-empty buffer. capacity=", buffer.avail_capacity());

				buffer.reset();
			}
			File_channel &c = *new (alloc)
				File_channel(alloc, { .writeable = true }, file_channels, *this);
			num_writers++;
			waiting_for_writers = false;
			return c;
		}

		if (filename == "/out") {
			File_channel &c = *new (alloc)
				File_channel(alloc, { .writeable = false }, file_channels, *this);

			if (0 == num_writers && buffer.empty()) {
				waiting_for_writers = true;
			}
			return c;
		}

		return Open_error::DENIED;
	}

	File_channel::Write_result write(File_channel &, Const_byte_range_ptr const &src)
	{
		size_t out = 0;

		if (buffer.avail_capacity() == 0)
			return File_channel::Write_error::RETRY;

		char const *buf_ptr = src.start;
		while (out < src.num_bytes && 0 < buffer.avail_capacity()) {
			buffer.add(*(buf_ptr++));
			++out;
		}

		if (out > 0) {
			_env.user().wakeup_vfs_user();
			notify_read();
		}

		return out;
	}

	File_channel::Read_result read(File_channel &, Byte_range_ptr const &dst)
	{
		size_t out = 0;

		char *buf_ptr = dst.start;
		while (out < dst.num_bytes && !buffer.empty()) {
			*(buf_ptr++) = buffer.get();
			++out;
		}

		if (out == 0) {

			/* Send only EOF when at least one writer opened the pipe */
			if ((num_writers == 0) && !waiting_for_writers)
				return File_channel::Read_eof();

			return File_channel::Read_error::RETRY;
		}

		/* new pipe space may unblock the writer */
		if (out > 0)
			_env.user().wakeup_vfs_user();

		return out;
	}
};


Vfs_pipe::File_channel::~File_channel()
{
	if (writer) {
		pipe.num_writers--;

		/* trigger reattempt of read to deliver EOF */
		if (pipe.num_writers == 0)
			pipe.submit_read_signal();
	} else {
		/* a close() may arrive before read() - make sure we deliver EOF */
		pipe.waiting_for_writers = false;
	}
	pipe.remove(*this);
}


void Vfs_pipe::File_channel::destruct()
{
	Pipe &p = pipe; /* don't access 'pipe' after destroy */
	destroy(_alloc, this);

	if (!p.new_channel_active && p.num_file_channels() == 0)
		p.destruct();
}


Vfs_pipe::File_channel::Write_result
Vfs_pipe::File_channel::write(At, Const_byte_range_ptr const &src)
{
	return File_channel::pipe.write(*this, src);
}


Vfs_pipe::File_channel::Read_result
Vfs_pipe::File_channel::read(At, Byte_range_ptr const &dst)
{
	return File_channel::pipe.read(*this, dst);
}


bool Vfs_pipe::File_channel::read_ready() const
{
	return !writer && !pipe.buffer.empty();
}


bool Vfs_pipe::File_channel::write_ready() const
{
	/*
	 * Unconditionally return true for the writer side because
	 * WRITE_ERR_WOULD_BLOCK is not yet supported.
	 */
	return writer;
}


void Vfs_pipe::File_channel::notify_read_ready()
{
	if (!writer && !read_ready_elem.enqueued())
		pipe.read_ready_waiters.enqueue(read_ready_elem);
}


struct Vfs_pipe::New_file_channel : Vfs::File_channel
{
	Allocator &_alloc;
	Pipe      &pipe;

	New_file_channel(Allocator &alloc, Vfs::Env &env, Attr attr,
	                 Pipe_space &pipe_space)
	:
		Vfs::File_channel(attr), _alloc(alloc),
		pipe(*(new (env.alloc()) Pipe(env, pipe_space)))
	{ }

	~New_file_channel()
	{
		pipe.new_channel_active = false;
		if (pipe.num_file_channels() == 0)
			pipe.destruct();
	}

	Read_result read(At, Byte_range_ptr const &dst) override
	{
		auto name = pipe.name();
		if (name.length() < dst.num_bytes) {
			memcpy(dst.start, name.string(), name.length());
			return name.length();
		}
		return Read_error::DENIED;
	}

	bool read_ready()  const override { return true;  }
	bool write_ready() const override { return false; }

	void destruct() override { destroy(_alloc, this); }
};


class Vfs_pipe::File_system : public Vfs::File_system
{
	protected:

		Vfs::Env &_env;

		Pipe_space _pipe_space { };

		/*
		 * verifies if a path meets access control requirements
		 */
		virtual bool _valid_path(const char* cpath) const = 0;

		virtual bool _pipe_id(const char* cpath, Pipe_space::Id &id) const = 0;

		template <typename FN>
		void _try_apply(Pipe_space::Id id, FN const &fn)
		{
			try { _pipe_space.apply<Pipe &>(id, fn); }
			catch (Pipe_space::Unknown_id) { }
		}

	public:

		File_system(Vfs::Env &env) : Vfs::File_system(Ident { "pipe" }), _env(env) { }

		Open_result open(char const *cpath, Open_attr attr, Allocator &alloc) override
		{
			/* distinguish reader from writer depending on the access mode */
			bool const writer = attr.writeable;

			if (!_valid_path(cpath))
				return Open_error::DENIED;

			Path const path { cpath };
			if (!path.has_single_element()) {
				/*
				 * find out if the last element is "/in" or "/out"
				 * and enforce read/write policy
				 */
				Path io { cpath };
				io.keep_only_last_element();

				if (io == "/in"  && !writer) return Open_error::DENIED;
				if (io == "/out" &&  writer) return Open_error::DENIED;
			}

			Vfs::File_channel *channel_ptr = nullptr;
			Open_error error = Open_error::DENIED;

			Pipe_space::Id id { ~0UL };
			if (_pipe_id(cpath, id))
				_try_apply(id, [&] (Pipe &pipe) {
					auto const type { writer ? "/in" : "/out" };
					pipe.open(type, alloc).with_result(
						[&] (Vfs::File_channel &c) { channel_ptr = &c; },
						[&] (Open_error e)         { error       = e;  });
				});

			if (channel_ptr)
				return *channel_ptr;

			return error;
		}

		Stat_result stat(const char *cpath, Stat &out) override
		{
			out = Stat { };

			if (!_valid_path(cpath))
				return Stat_result::DENIED;

			Stat_result result { Stat_result::DENIED };
			Path const path { cpath };

			if (path.has_single_element()) {
				Pipe_space::Id id { ~0UL };
				if (_pipe_id(cpath, id)) {
					out = Stat {
						.size              = file_size(0),
						.type              = Dirent_type::FILE,
						.rwx               = Node_rwx::rw(),
						.modification_time = { }
					};
					result = Stat_result::OK;
				}
			} else {
				/* find out if the last element is "/in" or "/out" */
				Path io { cpath };
				io.keep_only_last_element();

				Pipe_space::Id id { ~0UL };
				if (_pipe_id(cpath, id)) {
					_try_apply(id, [&io, &out, this, &result] (Pipe const &pipe) {
						if (io == "/in") {
							out = Stat {
								.size              = file_size(pipe.buffer.avail_capacity()),
								.type              = Dirent_type::FILE,
								.rwx               = Node_rwx::wo(),
								.modification_time = { }
							};
							result = Stat_result::OK;
						} else
						if (io == "/out") {
							out = Stat {
								.size              = file_size(PIPE_BUF_SIZE
								                             - pipe.buffer.avail_capacity()),
								.type              = Dirent_type::FILE,
								.rwx               = Node_rwx::ro(),
								.modification_time = { }
							};
							result = Stat_result::OK;
						}
					});
				}
			}

			return result;
		}

		bool dir_entry_exists(const char *cpath) override
		{
			Path const path { cpath };
			if (path == "/")
				return true;

			if (!_valid_path(cpath))
				return false;

			bool result = false;
			Pipe_space::Id id { ~0UL };
			if (_pipe_id(cpath, id))
				_try_apply(id, [&] (Pipe &) { result = true; });

			return result;
		}

		Follow_result follow(char const *path, Byte_range_ptr const &) override
		{
			return dir_entry_exists(path) ? Follow_error::NO_SYMLINK
			                              : Follow_error::DENIED;
		}
};


class Vfs_pipe::Pipe_file_system : public Vfs_pipe::File_system
{
	protected:

		virtual bool _pipe_id(const char* cpath, Pipe_space::Id &id) const override
		{
			return 0 != ascii_to(cpath + 1, id.value);
		}

		bool _valid_path(const char *cpath) const  override
		{
			/*
			 * a valid pipe path is either
			 * "/pipe_number",
			 * "/pipe_number/in"
			 * or
			 * "/pipe_number/out"
			 */

			Pipe_space::Id id { ~0UL };
			if (!_pipe_id(cpath, id))
				return false;

			Path io { cpath };
			if (io.has_single_element())
				return true;

			io.keep_only_last_element();
			if ((io == "/in" || io == "/out"))
				return true;

			return false;
		}

	public:

		Pipe_file_system(Vfs::Env &env) : File_system(env) { }

		void destruct() override { destroy(_env.alloc(), this); }

		Open_result open(const char *cpath, Open_attr attr, Allocator &alloc) override
		{
			Path const path { cpath };

			if (path == "/new") {
				try {
					return *new (alloc)
						New_file_channel(alloc, _env,
						                 { .writeable = attr.writeable }, _pipe_space);
				}
				catch (Out_of_ram)  { return Open_error::OUT_OF_RAM; }
				catch (Out_of_caps) { return Open_error::OUT_OF_CAPS; }
			}

			return Vfs_pipe::File_system::open(cpath, attr, alloc);
		}

		Stat_result stat(const char *cpath, Stat &out) override
		{
			out = Stat { };
			Path const path { cpath };

			if (path == "/new") {
				out = Stat {
					.size              = 1,
					.type              = Dirent_type::DEVICE,
					.rwx               = Node_rwx::ro(),
					.modification_time = { }
				};
				return Stat_result::OK;
			}

			return File_system::stat(cpath, out);
		}

		bool directory(char const *cpath) override
		{
			Path const path { cpath };
			if (path == "/") return true;
			if (path == "/new") return false;

			if (!path.has_single_element()) return false;

			bool result { false };
			Pipe_space::Id id { ~0UL };
			if (_pipe_id(cpath, id)) {
				_try_apply(id, [&result] (Pipe &) {
					result = true;
				});
			}

			return result;
		}

		bool dir_entry_exists(const char *cpath) override
		{
			Path path { cpath };
			if (path == "/new")
				return true;

			return File_system::dir_entry_exists(cpath);
		}

		Follow_result follow(char const *cpath, Byte_range_ptr const &) override
		{
			if (Path { cpath } == "/new") return Follow_error::NO_SYMLINK;

			return _valid_path(cpath) ? Follow_error::NO_SYMLINK
			                          : Follow_error::DENIED;
		}
};


class Vfs_pipe::Fifo_file_system : public Vfs_pipe::File_system
{
	private:

		struct Fifo_item
		{
			Registry<Fifo_item>::Element _element;
			Path const path;
			Pipe_space::Id const id;

			Fifo_item(Registry<Fifo_item> &registry,
			          Path const &path, Pipe_space::Id const &id)
			:
				_element(registry, *this), path(path), id(id)
			{ }
		};

		Registry<Fifo_item>  _items { };

	protected:

		bool _valid_path(const char *cpath) const  override
		{
			Pipe_space::Id id { ~0UL };
			if (!_pipe_id(cpath, id))
				return false;

			/*
			 * either we have no access control (single file in path)
			 * or we need to verify access control
			 */
			Path io { cpath };
			if (io.has_single_element())
				return true;

			/*
			 * a valid access control path is either
			 * "/.pipename/in/in"
			 * or
			 * "/.pipename/out/out"
			 */
			if (io.base()[1] != '.')
				return false;

			io.strip_last_element();
			if (io.has_single_element())
				return false;

			io.keep_only_last_element();
			if (!(io == "/in" || io == "/out"))
				return false;

			Path io_file { cpath };
			io_file.keep_only_last_element();
			if (io_file == io)
				return true;

			return false;
		}

		virtual bool _pipe_id(const char* cpath, Pipe_space::Id &id) const override
		{
			Path path { cpath };
			if (!path.has_single_element()) {
				/* remove /in/in or /out/out */
				path.strip_last_element();
				path.strip_last_element();
				/* remove the "." from /.pipe_name */
				if (strlen(path.base()) <= 2)
					return false;
				path = Path { path.base() + 2 };
			}

			bool result { false };
			_items.for_each([&path, &id, &result] (Fifo_item const &item) {
				if (item.path == path) {
					id = item.id;
					result = true;
				}
			});
			return result;
		}

	public:

		Fifo_file_system(Vfs::Env &env, Node const &config)
		:
			File_system(env)
		{
			config.for_each_sub_node("fifo", [&env, this] (Node const &fifo) {
				Path const path { fifo.attribute_value("name", String<MAX_PATH_LEN>()) };

				Pipe &pipe = *new (env.alloc()) Pipe(env, _pipe_space);
				new (env.alloc())
					Fifo_item(_items, path, pipe.space_elem.id());
			});
		}

		~Fifo_file_system()
		{
			_items.for_each([this] (Fifo_item &item) {
				destroy(_env.alloc(), &item);
			});
		}

		void destruct() override { destroy(_env.alloc(), this); }

		bool directory(char const *cpath) override
		{
			Path const path { cpath };
			if (path == "/") return true;
			if (_valid_path(cpath)) return false;

			Path io { cpath };
			io.keep_only_last_element();
			if (io == "/in") return true;
			if (io == "/out") return true;
			if (!path.has_single_element()) return false;

			return false;
		}

};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &, Node const &node) override
		{
			if (node.has_sub_node("fifo")) {
				return { *this, { *new (env.alloc()) Vfs_pipe::Fifo_file_system(env, node) } };
			} else {
				return { *this, { *new (env.alloc()) Vfs_pipe::Pipe_file_system(env) } };
			}
		}

		void _free(Instance &instance) override { instance.fs.destruct(); };
	};

	static Factory f;
	return &f;
}
