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

#include <vfs/file_system_factory.h>
#include <vfs/vfs_handle.h>
#include <os/path.h>
#include <os/ring_buffer.h>
#include <base/registry.h>

namespace Vfs_pipe {

	using namespace Genode;
	using namespace Genode::Vfs;

	using Open_result = Directory_service::Open_result;
	using Path        = Path<MAX_PATH_LEN>;

	enum { PIPE_BUF_SIZE = 8192U };
	using Pipe_buffer = Ring_buffer<unsigned char, PIPE_BUF_SIZE+1>;

	struct Pipe_handle;
	struct Dir_handle;

	using Handle_element = Fifo_element<Pipe_handle>;
	using Handle_fifo    = Fifo<Handle_element>;

	using Pipe_handle_registry_element = Registry<Pipe_handle>::Element;
	using Pipe_handle_registry         = Registry<Pipe_handle>;

	struct Pipe;
	using Pipe_space = Id_space<Pipe>;

	struct New_pipe_handle;

	class File_system;
	class Pipe_file_system;
	class Fifo_file_system;
}


struct Vfs_pipe::Pipe_handle : Vfs_handle, private Pipe_handle_registry_element
{
	Pipe &pipe;

	Handle_element read_ready_elem { *this };

	bool const writer;

	Pipe_handle(Vfs::File_system &fs,
	            Allocator &alloc,
	            unsigned flags,
	            Pipe_handle_registry &registry,
	            Pipe &p)
	:
		Vfs_handle(fs, alloc, flags),
		Pipe_handle_registry_element(registry, *this),
		pipe(p),
		writer(flags == Directory_service::OPEN_MODE_WRONLY)
	{ }

	virtual ~Pipe_handle();

	Write_result write(At, Const_byte_range_ptr const &) override;
	Read_result  read(At, Byte_range_ptr const &) override;

	Ftruncate_result ftruncate(file_size) override { return FTRUNCATE_ERR_NO_PERM; }

	bool read_ready()  const override;
	bool write_ready() const override;
	void notify_read_ready() override;
};


struct Vfs_pipe::Dir_handle : Vfs_handle
{
	using Vfs_handle::Vfs_handle;

	Read_result read(At, Byte_range_ptr const &) override { return Read_error::DENIED; }

	Ftruncate_result ftruncate(file_size) override { return FTRUNCATE_ERR_NO_PERM; }

	bool read_ready()  const override { return false; }
	bool write_ready() const override { return false; }
	void notify_read_ready() override { }
};


struct Vfs_pipe::Pipe
{
	Genode::Env    &env;
	Vfs::Env::User &vfs_user;
	Allocator      &alloc;

	Pipe_space::Element  space_elem;
	Pipe_buffer          buffer { };
	Pipe_handle_registry registry { };

	Handle_fifo read_ready_waiters { };

	unsigned num_writers = 0;
	bool waiting_for_writers = true;

	Io_signal_handler<Pipe> _read_notify_handler { env.ep(), *this, &Pipe::notify_read };

	bool new_handle_active { true };

	Pipe(Genode::Env &env, Vfs::Env::User &vfs_user,
	     Allocator &alloc, Pipe_space &space)
	:
		env(env), vfs_user(vfs_user), alloc(alloc), space_elem(*this, space)
	{ }

	~Pipe() = default;

	using Name = String<8>;
	Name name() const
	{
		return Name(space_elem.id().value);
	}

	void notify_read()
	{
		read_ready_waiters.dequeue_all([] (Handle_element &elem) {
			elem.object().read_ready_response(); });
	}

	void submit_read_signal()
	{
		_read_notify_handler.local_submit();
	}

	void submit_write_signal()
	{
		vfs_user.wakeup_vfs_user();
	}

	/**
	 * Check if pipe is referenced, if not, destroy
	 */
	void cleanup()
	{
		bool alive = new_handle_active;
		if (!alive)
			registry.for_each([&alive] (Pipe_handle&) {
				alive = true; });
		if (!alive)
			destroy(alloc, this);
	}

	/**
	 * Remove "/new" handle reference
	 */
	void remove_new_handle() {
		new_handle_active = false; }

	/**
	 * Detach a handle
	 */
	void remove(Pipe_handle &handle)
	{
		if (handle.read_ready_elem.enqueued())
			read_ready_waiters.remove(handle.read_ready_elem);
	}

	/**
	 * Open a write or read handle
	 */
	Open_result open(Vfs::File_system &fs,
	                 Path const &filename,
	                 Vfs::Vfs_handle **handle,
	                 Allocator &alloc)
	{
		if (filename == "/in") {

			if (0 == num_writers) {
				/* flush buffer */
				if (!buffer.empty())
					warning("flushing non-empty buffer. capacity=", buffer.avail_capacity());

				buffer.reset();
			}
			*handle = new (alloc)
				Pipe_handle(fs, alloc, Directory_service::OPEN_MODE_WRONLY, registry, *this);
			num_writers++;
			waiting_for_writers = false;
			return Open_result::OPEN_OK;
		}

		if (filename == "/out") {
			*handle = new (alloc)
				Pipe_handle(fs, alloc, Directory_service::OPEN_MODE_RDONLY, registry, *this);

			if (0 == num_writers && buffer.empty()) {
				waiting_for_writers = true;
			}
			return Open_result::OPEN_OK;
		}

		return Open_result::OPEN_ERR_UNACCESSIBLE;
	}

	Vfs_handle::Write_result write(Pipe_handle &, Const_byte_range_ptr const &src)
	{
		size_t out = 0;

		if (buffer.avail_capacity() == 0)
			return Vfs_handle::Write_error::RETRY;

		char const *buf_ptr = src.start;
		while (out < src.num_bytes && 0 < buffer.avail_capacity()) {
			buffer.add(*(buf_ptr++));
			++out;
		}

		if (out > 0) {
			vfs_user.wakeup_vfs_user();
			notify_read();
		}

		return out;
	}

	Vfs_handle::Read_result read(Pipe_handle &, Byte_range_ptr const &dst)
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
				return 0; /* EOF */

			return Vfs_handle::Read_error::RETRY;
		}

		/* new pipe space may unblock the writer */
		if (out > 0)
			vfs_user.wakeup_vfs_user();

		return out;
	}
};


Vfs_pipe::Pipe_handle::~Pipe_handle()
{
	pipe.remove(*this);
}


Vfs_pipe::Vfs_handle::Write_result
Vfs_pipe::Pipe_handle::write(At, Const_byte_range_ptr const &src)
{
	return Pipe_handle::pipe.write(*this, src);
}


Vfs_pipe::Vfs_handle::Read_result
Vfs_pipe::Pipe_handle::read(At, Byte_range_ptr const &dst)
{
	return Pipe_handle::pipe.read(*this, dst);
}


bool Vfs_pipe::Pipe_handle::read_ready() const
{
	return !writer && !pipe.buffer.empty();
}


bool Vfs_pipe::Pipe_handle::write_ready() const
{
	/*
	 * Unconditionally return true for the writer side because
	 * WRITE_ERR_WOULD_BLOCK is not yet supported.
	 */
	return writer;
}


void Vfs_pipe::Pipe_handle::notify_read_ready()
{
	if (!writer && !read_ready_elem.enqueued())
		pipe.read_ready_waiters.enqueue(read_ready_elem);
}


struct Vfs_pipe::New_pipe_handle : Vfs_handle
{
	Pipe &pipe;

	New_pipe_handle(Vfs::File_system &fs,
	                Genode::Env      &env,
	                Vfs::Env::User   &vfs_user,
	                Allocator        &alloc,
	                unsigned          flags,
	                Pipe_space       &pipe_space)
	:
		Vfs_handle(fs, alloc, flags),
		pipe(*(new (alloc) Pipe(env, vfs_user, alloc, pipe_space)))
	{ }

	~New_pipe_handle()
	{
		pipe.remove_new_handle();
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

	bool read_ready()  const override { return true; }
	bool write_ready() const override { return false; }
};


class Vfs_pipe::File_system : public Vfs::File_system
{
	protected:

		Vfs::Env  &_env;

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

		File_system(Vfs::Env &env) : _env(env) { }

		const char* type() override { return "pipe"; }

		/***********************
		 ** Directory service **
		 ***********************/

		Dataspace_capability dataspace(char const*) override {
			return Dataspace_capability(); }

		void release(char const*, Dataspace_capability) override { }

		Open_result open(const char *cpath, unsigned mode,
		                 Vfs::Vfs_handle **handle,
		                 Allocator &alloc) override
		{
			/* distinguish reader from writer depending on the access mode */
			bool const writer = (mode & OPEN_MODE_ACCMODE) != OPEN_MODE_RDONLY;

			if (!_valid_path(cpath))
				return OPEN_ERR_UNACCESSIBLE;

			Path const path { cpath };
			if (!path.has_single_element()) {
				/*
				 * find out if the last element is "/in" or "/out"
				 * and enforce read/write policy
				 */
				Path io { cpath };
				io.keep_only_last_element();

				if (io == "/in"  && !writer) return OPEN_ERR_NO_PERM;
				if (io == "/out" &&  writer) return OPEN_ERR_NO_PERM;
			}

			auto result { OPEN_ERR_UNACCESSIBLE };
			Pipe_space::Id id { ~0UL };
			if (_pipe_id(cpath, id)) {
				_try_apply(id, [&] (Pipe &pipe) {
					auto const type { writer ? "/in" : "/out" };
					result = pipe.open(*this, type, handle, alloc);
				});
			}

			return result;
		}

		Opendir_result opendir(char const *cpath, bool create,
		                       Vfs_handle **handle,
		                       Allocator &alloc) override
		{
			/* open dummy handles on directories */
			if (create)
				return OPENDIR_ERR_PERMISSION_DENIED;

			Path io { cpath };
			if (io == "/") {
				*handle = new (alloc) Dir_handle(*this, alloc, 0);
				return OPENDIR_OK;
			}

			auto result { OPENDIR_ERR_PERMISSION_DENIED };
			/* create a path that matches with _pipe_id() */
			Path pseudo_path { cpath };
			io.keep_only_last_element();
			pseudo_path.append(io.string());
			Pipe_space::Id id { ~0UL };
			if (_pipe_id(pseudo_path.string(), id)) {
				_try_apply(id, [&handle, &alloc, this, &result] (Pipe &/*pipe*/) {
					*handle = new (alloc)
						Dir_handle(*this, alloc, 0);
					result = OPENDIR_OK;
				});
			}
			return result;
		}

		void close(Vfs_handle *vfs_handle) override
		{
			Pipe *pipe = nullptr;
			if (Pipe_handle *handle = dynamic_cast<Pipe_handle*>(vfs_handle)) {
				pipe = &handle->pipe;
				if (handle->writer) {
					pipe->num_writers--;

					/* trigger reattempt of read to deliver EOF */
					if (pipe->num_writers == 0)
						pipe->submit_read_signal();
				} else {
					/* a close() may arrive before read() - make sure we deliver EOF */
					pipe->waiting_for_writers = false;
				}
			} else
			if (New_pipe_handle *handle = dynamic_cast<New_pipe_handle*>(vfs_handle))
				pipe = &handle->pipe;

			destroy(vfs_handle->alloc(), vfs_handle);

			if (pipe)
				pipe->cleanup();
		}

		Stat_result stat(const char *cpath, Stat &out) override
		{
			out = Stat { };

			if (!_valid_path(cpath))
				return STAT_ERR_NO_ENTRY;

			Stat_result result { STAT_ERR_NO_ENTRY };
			Path const path { cpath };

			if (path.has_single_element()) {
				Pipe_space::Id id { ~0UL };
				if (_pipe_id(cpath, id)) {
					out = Stat {
						.size              = file_size(0),
						.type              = Node_type::CONTINUOUS_FILE,
						.rwx               = Node_rwx::rw(),
						.device            = addr_t(this),
						.modification_time = { }
					};
					result = STAT_OK;
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
								.type              = Node_type::CONTINUOUS_FILE,
								.rwx               = Node_rwx::wo(),
								.device            = addr_t(this),
								.modification_time = { }
							};
							result = STAT_OK;
						} else
						if (io == "/out") {
							out = Stat {
								.size              = file_size(PIPE_BUF_SIZE
								                             - pipe.buffer.avail_capacity()),
								.type              = Node_type::CONTINUOUS_FILE,
								.rwx               = Node_rwx::ro(),
								.device            = addr_t(this),
								.modification_time = { }
							};
							result = STAT_OK;
						}
					});
				}
			}

			return result;
		}

		Unlink_result unlink(const char*) override {
			return UNLINK_ERR_NO_ENTRY; }

		Rename_result rename(const char*, const char*) override {
			return RENAME_ERR_NO_ENTRY; }

		unsigned num_dirent(char const *) override { return 0; }

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

		Open_result open(const char *cpath,
		                 unsigned mode,
		                 Vfs::Vfs_handle **handle,
		                 Allocator &alloc) override
		{
			Path const path { cpath };

			if (path == "/new") {
				*handle = new (alloc)
					New_pipe_handle(*this, _env.env(), _env.user(), alloc, mode, _pipe_space);
				return OPEN_OK;
			}

			return File_system::open(cpath, mode, handle, alloc);
		}

		Stat_result stat(const char *cpath, Stat &out) override
		{
			out = Stat { };
			Path const path { cpath };

			if (path == "/new") {
				out = Stat {
					.size              = 1,
					.type              = Node_type::TRANSACTIONAL_FILE,
					.rwx               = Node_rwx::ro(),
					.device            = addr_t(this),
					.modification_time = { }
				};
				return STAT_OK;
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

				Pipe &pipe = *new (env.alloc())
					Pipe(env.env(), env.user(), env.alloc(), _pipe_space);
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


extern "C" Genode::Vfs::File_system_factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system_factory
	{
		Vfs::File_system *create(Vfs::Env &env, Vfs::Parent_fs &, Node const &node) override
		{
			if (node.has_sub_node("fifo")) {
				return new (env.alloc()) Vfs_pipe::Fifo_file_system(env, node);
			} else {
				return new (env.alloc()) Vfs_pipe::Pipe_file_system(env);
			}
		}
	};

	static Factory f;
	return &f;
}
