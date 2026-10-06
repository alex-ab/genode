/*
 * \brief  Socket-based file system
 * \author Christian Helmuth
 * \author Josef Soentgen
 * \author Emery Hemingway
 * \author Sebastian Sumpf
 * \date   2016-02-01
 *
 * 2023-11-08: adjust to socket C-API
 * 2025-02-09: generalized for lxip & lwip
 */

/*
 * Copyright (C) 2015-2025 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

/* Genode includes */
#include <base/log.h>
#include <format/snprintf.h>
#include <genode_c_api/socket.h>
#include <net/ipv4.h>
#include <util/string.h>
#include <timer_session/connection.h>

#include "vfs_ip.h"
#include "socket_error.h"
#include "sockopt.h"

namespace Vfs_ip {

	using namespace Vfs;
	using namespace Genode;

	struct Msg_header;
	struct Protocol_dir;
	struct Socket_dir;

	class Protocol_dir_impl;

	enum {
		MAX_SOCKETS         = 128,       /* 3 */
		MAX_SOCKET_NAME_LEN = 3 + 1,     /* + \0 */
		MAX_DATA_LEN        = 32,        /* 255.255.255.255:65536 + something */
	};

	struct Node;
	struct Directory;
	struct File;

	class Ip_file;
	class Ip_data_file;
	class Ip_bind_file;
	class Ip_accept_file;
	class Ip_connect_file;
	class Ip_listen_file;
	class Ip_local_file;
	class Ip_remote_file;
	class Ip_peek_file;
	class Ip_error_file;

	class Ip_sockopt_dir;
	class Ip_socket_dir;
	struct Ip_socket_file_channel;

	struct Ip_address_info;
	class  Ip_link_state_file;
	class  Ip_address_file;

	class Ip_file_channel;
	class Ip_dir_channel;
	class Ip_file_system;

	using Ip_file_channels = List<List_element<Ip_file_channel> >;
}


static inline long get_port(char const *p)
{
	long tmp = -1;

	while (*++p) {
		if (*p == ':') {
			Genode::ascii_to_unsigned(++p, tmp, 10);
			break;
		}
	}
	return tmp;
}


static inline unsigned get_addr(char const *p)
{
	unsigned char to[4] = { 0, 0, 0, 0};

	for (unsigned char &c : to) {

		unsigned result = 0;
		p += Genode::ascii_to_unsigned(p, result, 10);

		c = (unsigned char)result;

		if (*p == '.') ++p;
		if (*p == 0) break;
	};

	return (to[0]<<0)|(to[1]<<8)|(to[2]<<16)|(to[3]<<24);
}


static inline long get_family(char const *p)
{
	long tmp = -1;

	while (*p) {
		if (*p == ';') {
			Genode::ascii_to_unsigned(++p, tmp, 1);
			break;
		}
		p++;
	}
	return tmp;
}


struct Vfs_ip::Msg_header
{
	genode_iovec  iovec;
	genode_msghdr msg { };

	Msg_header(void const *data, unsigned long size)
	: iovec { const_cast<void *>(data), size }
	{
		msg.iov    = &iovec;
		msg.iovlen = 1;
	}

	Msg_header(genode_sockaddr &name, void const *data, unsigned long size)
	: Msg_header(data, size)
	{
		msg.name = &name;
	}

	void name(genode_sockaddr &name)
	{
		msg.name =&name;
	}

	genode_msghdr *header() { return &msg; }
};


/***************
 ** Vfs nodes **
 ***************/

struct Vfs_ip::Node
{
	char const *_name;

	Node(char const *name) : _name(name) { }

	virtual ~Node() { }

	char const *name() { return _name; }

	virtual void close() { }

	Node(const Node&) = delete;
	Node operator=(const Node&) = delete;
};


struct Vfs_ip::File : Vfs_ip::Node
{
	Ip_file_channels channels { };

	File(char const *name) : Node(name) { }

	virtual ~File() { }

	/**
	 * Read or write operation would block exception
	 */
	struct Would_block { };

	/**
	 * Check for data to read or write
	 */
	virtual bool read_ready()  const { return true; }
	virtual bool write_ready() const { return true; };

	virtual long write(Ip_file_channel &, Const_byte_range_ptr const &, file_size)
	{
		error(name(), " not writeable");
		return -1;
	}

	virtual long read(Ip_file_channel &, Byte_range_ptr const &, file_size)
	{
		error(name(), " not readable");
		return -1;
	}

	virtual Sync_result sync() { return Sync_result::OK; }
};


struct Vfs_ip::Directory : Vfs_ip::Node
{
	Directory(char const *name) : Node(name) { }

	virtual ~Directory() { };

	virtual Vfs_ip::Node *child(char const *) = 0;
	virtual unsigned num_dirent()             = 0;

	using Open_attr = File_system::Open_attr;

	virtual Open_result open(File_system &fs, char const *, Open_attr, Allocator &) = 0;

	virtual Dir_channel::Read_dir_result read(Dir_channel::At, Dirent_range_ptr const &) = 0;
};


struct Vfs_ip::Protocol_dir : Vfs_ip::Directory
{
	enum Type { TYPE_STREAM, TYPE_DGRAM };

	virtual char const *top_dir() = 0;
	virtual Type type() = 0;
	virtual unsigned adopt_socket(Socket_dir &) = 0;
	virtual void release(unsigned id) = 0;

	Protocol_dir(char const *name) : Vfs_ip::Directory(name) { }
};


struct Vfs_ip::Socket_dir : Vfs_ip::Directory
{
	virtual Protocol_dir &parent() = 0;
	virtual char const *top_dir() = 0;
	virtual void     connect(bool) = 0;
	virtual void     listen(bool) = 0;
	virtual genode_sockaddr &remote_addr() = 0;
	virtual void     close() override = 0;
	virtual bool     closed() const = 0;
	virtual Errno    socket_error(Errno const err) = 0;

	Socket_dir(char const *name) : Vfs_ip::Directory(name) { }
};


static Genode::Fifo<Genode::Fifo_element<Vfs_ip::Ip_file_channel>> *_read_ready_waiters_ptr;


struct Vfs_ip::Ip_file_channel : Vfs::File_channel
{
	Ip_file_channel(Ip_file_channel const &);
	Ip_file_channel &operator = (Ip_file_channel const &);

	Allocator &_alloc;

	Vfs_ip::File *file;

	/* file association element */
	List_element<Ip_file_channel> file_le { this };

	/* notification elements */
	using Fifo_element = Genode::Fifo_element<Ip_file_channel>;
	using Fifo         = Genode::Fifo<Fifo_element>;

	Fifo_element read_ready_elem { *this };

	char content_buffer[MAX_DATA_LEN];

	Ip_file_channel(Allocator &alloc, Attr attr, Vfs_ip::File *file)
	:
		Vfs::File_channel(attr),
		_alloc(alloc), file(file)
	{
		if (file)
			file->channels.insert(&file_le);
	}

	~Ip_file_channel()
	{
		if (file)
			file->channels.remove(&file_le);
	}

	bool read_ready() const override {
		return (file) ? file->read_ready() : false; }

	bool write_ready() const override {
		return (file) ? file->write_ready() : false; }

	Read_result read(At const at, Byte_range_ptr const &dst) override
	{
		if (!file) return Read_error::DENIED;

		try {
			long const res = file->read(*this, dst, at.pos);
			if (res < 0)
				return Read_error::DENIED;
			return res;
		}
		catch (File::Would_block) { return Read_error::RETRY; }
	}

	Write_result write(At const at, Const_byte_range_ptr const &src) override
	{
		if (!file)
			return Write_error::DENIED;
		try {
			long res = file->write(*this, src, at.pos);
			if (res < 0)
				return Write_error::DENIED;
			return res;
		}
		catch (File::Would_block) { return Write_error::RETRY; }
	}

	bool write_content_line(Const_byte_range_ptr const &src)
	{
		if (src.num_bytes > sizeof(content_buffer) - 2)
			return false;

		memcpy(content_buffer, src.start, src.num_bytes);
		content_buffer[src.num_bytes + 0] = '\n';
		content_buffer[src.num_bytes + 1] = '\0';
		return true;
	}

	virtual Sync_result sync() override
	{
		return file ? file->sync() : Sync_result::OK;
	}

	void notify_read_ready() override
	{
		if (!read_ready_elem.enqueued())
			_read_ready_waiters_ptr->enqueue(read_ready_elem);
	}

	Resize_result resize(file_size) override
	{
		/* report ok because libc always executes ftruncate() when opening rw */
		return Resize_result::OK;
	}

	void destruct() override
	{
		_read_ready_waiters_ptr->remove(read_ready_elem);
		destroy(_alloc, this);
	}
};


struct Vfs_ip::Ip_dir_channel : Vfs::Dir_channel
{
	Allocator         &_alloc;
	Vfs_ip::Directory &_dir;

	Ip_dir_channel(Allocator &alloc, Vfs_ip::Directory &dir)
	:
		_alloc(alloc), _dir(dir)
	{ }

	void destruct() override { destroy(_alloc, this); }

	Read_dir_result read_dir(At at, Dirent_range_ptr const &dst) override
	{
		return _dir.read(at, dst);
	}
};


static void poll_all()
{
	_read_ready_waiters_ptr->for_each(
			[&] (Vfs_ip::Ip_file_channel::Fifo_element &elem) {
		Vfs_ip::Ip_file_channel &c = elem.object();
		if (c.file) {
			if (c.file->read_ready()) {
				/* do not notify again until notify_read_ready */
				_read_ready_waiters_ptr->remove(elem);

				c.read_ready_response();
			}
		}
	});
}

/*****************************
 ** Ip vfs specific nodes **
 *****************************/

class Vfs_ip::Ip_file : public Vfs_ip::File
{
	protected:

		Socket_dir           &_parent;
		genode_socket_handle &_sock;

		Errno _write_err = GENODE_ENONE;

		Errno socket_error(Errno const err) { return _parent.socket_error(err); }

	public:

		Ip_file(Socket_dir &p, genode_socket_handle &s, char const *name)
		: Vfs_ip::File(name), _parent(p), _sock(s) { }

		virtual ~Ip_file() { }

		/**
		 * Dissolve relationship between channel and file, file and polling list.
		 */
		void dissolve_channels()
		{
			List_element<Vfs_ip::Ip_file_channel> *le = channels.first();
			while (le) {
				Vfs_ip::Ip_file_channel &c = *le->object();
				channels.remove(&c.file_le);
				c.file = nullptr;
				le = channels.first();
			}
		}

		Sync_result sync() override
		{
			if (_write_err) warning("vfs_ip: write error happened before sync");
			return Sync_result::OK;
		}
};


class Vfs_ip::Ip_data_file final : public Vfs_ip::Ip_file
{
	public:

		Ip_data_file(Socket_dir &p, genode_socket_handle &s)
		: Ip_file(p, s, "data") { }

		/********************
		 ** File interface **
		 ********************/

		bool read_ready() const override
		{
			return genode_socket_poll(&_sock) & genode_socket_pollin_set();
		}

		bool write_ready() const override
		{
			return genode_socket_poll(&_sock) & genode_socket_pollout_set();
		}

		long write(Ip_file_channel &, Const_byte_range_ptr const &src, file_size) override
		{
			unsigned long bytes_sent = 0;
			Msg_header    msg_send { src.start, src.num_bytes };

			/* destination address is only required for UDP */
			if (_parent.parent().type() == Protocol_dir::TYPE_DGRAM)
				msg_send.name(_parent.remote_addr());

			_write_err = socket_error(genode_socket_sendmsg(&_sock, msg_send.header(),
			                                                &bytes_sent));

			/* propagate EAGAIN */
			if (_write_err == GENODE_EAGAIN)
				throw Would_block();

			return _write_err == GENODE_ENONE ? bytes_sent : -1;
		}

		long read(Ip_file_channel &, Byte_range_ptr const &dst, file_size) override
		{
			unsigned long bytes = 0;
			Msg_header    msg_recv { dst.start, dst.num_bytes };

			Errno err = socket_error(genode_socket_recvmsg(&_sock, msg_recv.header(),
			                                               &bytes, false));
			if (err == GENODE_EAGAIN)
				throw Would_block();

			return bytes;
		}
};


class Vfs_ip::Ip_peek_file final : public Vfs_ip::Ip_file
{
	public:

		Ip_peek_file(Socket_dir &p, genode_socket_handle &s)
		: Ip_file(p, s, "peek") { }

		/********************
		 ** File interface **
		 ********************/

		/* can always peek */
		bool read_ready()  const override { return true;  }
		bool write_ready() const override { return false; }

		long write(Ip_file_channel &, Const_byte_range_ptr const &, file_size) override
		{
			return -1;
		}

		long read(Ip_file_channel &, Byte_range_ptr const &dst, file_size) override
		{
			unsigned long bytes_avail = 0;
			Msg_header    msg_recv { dst.start, dst.num_bytes };

			Errno err = socket_error(genode_socket_recvmsg(&_sock, msg_recv.header(),
			                                               &bytes_avail, true));

			if (err == GENODE_EAGAIN)
				return -1;

			return bytes_avail;
		}
};


class Vfs_ip::Ip_bind_file final : public Vfs_ip::Ip_file
{
	public:

		Ip_bind_file(Socket_dir &p, genode_socket_handle &s)
		: Ip_file(p, s, "bind") { }

		/********************
		 ** File interface **
		 ********************/

		long write(Ip_file_channel &c, Const_byte_range_ptr const &src, file_size) override
		{
			if (!c.write_content_line(src)) return -1;

			long port = get_port(c.content_buffer);
			if (port == -1) return -1;

			/* port is free, try to bind it */
			genode_sockaddr addr;
			addr.family  = AF_INET;
			addr.in.port = host_to_big_endian<genode_uint16_t>(uint16_t(port));
			addr.in.addr = get_addr(c.content_buffer);

			_write_err = socket_error(genode_socket_bind(&_sock, &addr));
			if (_write_err != GENODE_ENONE) return -1;

			return src.num_bytes;
		}

		long read(Ip_file_channel &c, Byte_range_ptr const &dst, file_size) override
		{
			if (dst.num_bytes < sizeof(c.content_buffer))
				return -1;

			size_t const n = strlen(c.content_buffer);
			memcpy(dst.start, c.content_buffer, n);

			return n;
		}
};


class Vfs_ip::Ip_listen_file final : public Vfs_ip::Ip_file
{
	private:

		unsigned long _backlog = ~0UL;

	public:

		Ip_listen_file(Socket_dir &p, genode_socket_handle &s)
		: Ip_file(p, s, "listen") { }

		/********************
		 ** File interface **
		 ********************/

		long write(Ip_file_channel &c, Const_byte_range_ptr const &src, file_size) override
		{
			/* write-once */
			if (_backlog != ~0UL) return -1;

			if (!c.write_content_line(src)) return -1;

			ascii_to_unsigned(
				c.content_buffer, _backlog, sizeof(c.content_buffer));

			if (_backlog == ~0UL) return -1;

			_write_err = socket_error(genode_socket_listen(&_sock, (int)_backlog));
			if (_write_err != GENODE_ENONE) {
				c.write_content_line(Const_byte_range_ptr("", 0));
				return -1;
			}

			_parent.listen(true);

			return src.num_bytes;
		}

		long read(Ip_file_channel &, Byte_range_ptr const &dst, file_size) override
		{
			return Format::snprintf(dst.start, dst.num_bytes, "%lu\n", _backlog);
		}
};


class Vfs_ip::Ip_connect_file final : public Vfs_ip::Ip_file
{
	private:

		bool _connecting   = false;
		bool _is_connected = false;

	public:

		Ip_connect_file(Socket_dir &p, genode_socket_handle &s)
		: Ip_file(p, s, "connect") { }

		/********************
		 ** File interface **
		 ********************/

		bool read_ready() const override
		{
			/*
			 * The connect file is considered readable when the socket is
			 * writeable (connected or error).
			 */
			return genode_socket_poll(&_sock) & genode_socket_pollout_set();
		}

		bool write_ready() const override { return true; };

		long write(Ip_file_channel &c, Const_byte_range_ptr const &src, file_size) override
		{
			if (!c.write_content_line(src)) return -1;

			long const port = get_port(c.content_buffer);
			long const family = get_family(c.content_buffer);
			if (port == -1) return -1;

			genode_sockaddr addr;
			addr.family  = family == 0 ? AF_UNSPEC : AF_INET;
			addr.in.port = host_to_big_endian<genode_uint16_t>(uint16_t(port));
			addr.in.addr = get_addr(c.content_buffer);

			_write_err = socket_error(genode_socket_connect(&_sock, &addr));

			switch (_write_err) {
			case GENODE_EINPROGRESS:
				_connecting = true;
				_write_err = GENODE_ENONE;
				return src.num_bytes;

			case GENODE_EALREADY:
				return -1;

			case GENODE_EISCONN:
				/*
				 * Connecting on an already connected socket is an error.
				 * If we get this error after we got EINPROGRESS it is
				 * fine.
				 */
				if (_is_connected || !_connecting) return -1;
				_is_connected = true;
				_write_err = GENODE_ENONE;
				break;

			default:
				if (_write_err != GENODE_ENONE) return -1;
				_is_connected = true;
				break;
			}

			genode_sockaddr &remote_addr = _parent.remote_addr();
			remote_addr.in.port          = host_to_big_endian<genode_uint16_t>(uint16_t(port));
			remote_addr.in.addr          = get_addr(c.content_buffer);
			remote_addr.family           = AF_INET;

			_parent.connect(true);

			return src.num_bytes;
		}

		long read(Ip_file_channel &, Byte_range_ptr const &dst, file_size /* ignored */) override
		{
			Errno err;
			unsigned long size = 1;
			unsigned data = 0;

			/*
			 * Use msg_peek to determine connection state - all genode_socket*
			 * operations are non-blocking
			 */
			Msg_header msg { &data, 1 };
			err = genode_socket_recvmsg(&_sock, msg.header(), &size, true);

			if (err == GENODE_EAGAIN || (err == GENODE_ENONE && size == 1))
				return Format::snprintf(dst.start, dst.num_bytes, "connected");

			if (err == GENODE_ECONNREFUSED)
				return Format::snprintf(dst.start, dst.num_bytes, "connection refused");

			if (err == GENODE_ENONE && size == 0)
				return Format::snprintf(dst.start, dst.num_bytes, "not connected");

			error("Ip_connect_file::read unhandled error: ", unsigned(err));

			return Format::snprintf(dst.start, dst.num_bytes, "unknown error");
		}
};


class Vfs_ip::Ip_local_file final : public Vfs_ip::Ip_file
{
	public:

		Ip_local_file(Socket_dir &p, genode_socket_handle &s)
		: Ip_file(p, s, "local") { }

		/********************
		 ** File interface **
		 ********************/

		long read(Ip_file_channel &c, Byte_range_ptr const &dst, file_size) override
		{
			if (dst.num_bytes < sizeof(c.content_buffer))
				return -1;

			genode_sockaddr addr;
			if (genode_socket_getsockname(&_sock, &addr) != GENODE_ENONE) return -1;

			unsigned char const *a = (unsigned char *)&addr.in.addr;
			unsigned char const *p = (unsigned char *)&addr.in.port;
			return Format::snprintf(dst.start, dst.num_bytes,
			                        "%d.%d.%d.%d:%u\n",
			                        a[0], a[1], a[2], a[3], (p[0]<<8)|(p[1]<<0));
		}
};


class Vfs_ip::Ip_remote_file final : public Vfs_ip::Ip_file
{
	public:

		Ip_remote_file(Socket_dir &p, genode_socket_handle &s)
		: Ip_file(p, s, "remote") { }

		/********************
		 ** File interface **
		 ********************/

		bool read_ready() const override
		{
			switch (_parent.parent().type()) {
			case Protocol_dir::TYPE_DGRAM:
				return genode_socket_poll(&_sock) & genode_socket_pollin_set();

			case Protocol_dir::TYPE_STREAM:
				return true;
			}

			return false;
		}

		bool write_ready() const override { return false; }

		long read(Ip_file_channel &c, Byte_range_ptr const &dst, file_size) override
		{
			genode_sockaddr addr { .family = AF_INET };

			switch (_parent.parent().type()) {
			case Protocol_dir::TYPE_DGRAM:
				{
					/* peek the sender address of the next packet */
					unsigned long bytes = 0;
					Msg_header msg_recv = { addr, c.content_buffer, sizeof(c.content_buffer) };

					Errno err = genode_socket_recvmsg(&_sock, msg_recv.header(), &bytes, true);
					if (err == GENODE_EAGAIN)
						throw Would_block();

					if (err) return -1;
				}
				break;
			case Protocol_dir::TYPE_STREAM:
				{
					if (genode_socket_getpeername(&_sock, &addr) != GENODE_ENONE)
						return -1;
				}
				break;
			}

			unsigned char const *a = (unsigned char *)&addr.in.addr;
			unsigned char const *p = (unsigned char *)&addr.in.port;
			return Format::snprintf(dst.start, dst.num_bytes,
			                        "%d.%d.%d.%d:%u\n",
			                        a[0], a[1], a[2], a[3], (p[0]<<8)|(p[1]<<0));
		}

		long write(Ip_file_channel &c, Const_byte_range_ptr const &src, file_size) override
		{
			if (!c.write_content_line(src)) return -1;

			long const port = get_port(c.content_buffer);
			if (port == -1) return -1;

			genode_sockaddr &remote_addr = _parent.remote_addr();
			remote_addr.in.port          = host_to_big_endian<genode_uint16_t>(uint16_t(port));
			remote_addr.in.addr          = get_addr(c.content_buffer);
			remote_addr.family           = AF_INET;

			return src.num_bytes;
		}
};


class Vfs_ip::Ip_accept_file final : public Vfs_ip::Ip_file
{
	public:

		Ip_accept_file(Socket_dir &p, genode_socket_handle &s)
		: Ip_file(p, s, "accept") { }

		/********************
		 ** File interface **
		 ********************/

		bool read_ready() const override
		{
			return genode_socket_poll(&_sock) & genode_socket_pollin_set();
		}

		bool write_ready() const override { return false; }

		long read(Ip_file_channel &, Byte_range_ptr const &dst, file_size) override
		{
			if (genode_socket_poll(&_sock) & genode_socket_pollin_set()) {
				copy_cstring(dst.start, "1\n", dst.num_bytes);
				return strlen(dst.start);
			}

			throw Would_block();
		}
};


class Vfs_ip::Ip_error_file : public Vfs_ip::File
{
	private:

		Error_file_system _error_fs;

	public:

		Ip_error_file(Parent_fs &parent_fs, char const *name)
		:
			File(name), _error_fs(parent_fs)
		{ }

		using Open_attr = File_system::Open_attr;

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) {
			return _error_fs.open(path, attr, alloc); }

		Errno socket_error(Errno const err) { return _error_fs.socket_error(err); }
};


class Vfs_ip::Ip_sockopt_dir : public Vfs_ip::Directory
{
		Sockopt_file_system _sockopt_fs;

		File _dummy { "dummy" };

	public:

		Ip_sockopt_dir(Vfs::Env &env, Parent_fs &parent_fs, genode_socket_handle &sock)
		:
			Directory("sockopts"),
			_sockopt_fs(env, parent_fs, sock)
		{ }

		Vfs_ip::Node *child(char const *name) override
		{
			File_system::Stat out;
			if (_sockopt_fs.stat(name, out) == Stat_result::OK) {
				/* sockopts directory */
				if (out.type == Dirent_type::DIRECTORY) return this;

				return &_dummy;
			}

			error("Ip_socket_dir::child: failed for ", name);
			return nullptr;
		}

		Open_result open(File_system &, char const *path, Open_attr attr, Allocator &alloc) override
		{
			return _sockopt_fs.open(path, attr, alloc);
		}

		Dir_channel::Read_dir_result read(Dir_channel::At, Dirent_range_ptr const &) override
		{
			error(__PRETTY_FUNCTION__, " called not implemented");
			return Dir_channel::Read_dir_end();
		}

		unsigned num_dirent() override
		{
			error(__PRETTY_FUNCTION__, " called not implemented");
			return 0;
		}
};


class Vfs_ip::Ip_socket_dir final : public Socket_dir
{
	public:

		enum {
			ACCEPT_NODE, BIND_NODE, CONNECT_NODE,
			DATA_NODE, PEEK_NODE,
			LOCAL_NODE, LISTEN_NODE, REMOTE_NODE,
			ACCEPT_SOCKET_NODE,
			MAX_FILES
		};

	private:

		Vfs::Env             &_env;
		Parent_fs            &_parent_fs;
		Allocator            &_alloc;
		Protocol_dir         &_parent;
		genode_socket_handle &_sock;

		Vfs_ip::File *_files[MAX_FILES];

		genode_sockaddr _remote_addr { };

		unsigned _num_nodes()
		{
			unsigned num = 0;
			for (Vfs_ip::File *n : _files) num += (n != nullptr);
			return num;
		}

		Ip_accept_file  _accept_file  { *this, _sock };
		Ip_bind_file    _bind_file    { *this, _sock };
		Ip_connect_file _connect_file { *this, _sock };
		Ip_data_file    _data_file    { *this, _sock };
		Ip_peek_file    _peek_file    { *this, _sock };
		Ip_listen_file  _listen_file  { *this, _sock };
		Ip_local_file   _local_file   { *this, _sock };
		Ip_remote_file  _remote_file  { *this, _sock };

		/* next generation */
		Ip_sockopt_dir    _sockopt_fs { _env, _parent_fs, _sock };
		Ip_error_file     _error_fs   { _parent_fs, "error" };

		struct Accept_socket_file : Vfs_ip::File
		{
			Accept_socket_file() : Vfs_ip::File("accept_socket") { }

		} _accept_socket_file { };

		char _name[MAX_SOCKET_NAME_LEN];

		Open_result _accept_new_socket(Allocator &);

	public:

		unsigned const id;

		Ip_socket_dir(Vfs::Env &env,
		              Parent_fs &parent_fs,
		              Allocator &alloc,
		              Protocol_dir &parent,
		              genode_socket_handle &sock)
		:
			Socket_dir(_name),
			_env(env), _parent_fs(parent_fs), _alloc(alloc), _parent(parent),
			_sock(sock), id(parent.adopt_socket(*this))
		{
			Format::snprintf(_name, sizeof(_name), "%u", id);

			for (Vfs_ip::File * &file : _files) file = nullptr;

			_files[ACCEPT_NODE]  = &_accept_file;
			_files[BIND_NODE]    = &_bind_file;
			_files[CONNECT_NODE] = &_connect_file;
			_files[DATA_NODE]    = &_data_file;
			_files[PEEK_NODE]    = &_peek_file;
			_files[LISTEN_NODE]  = &_listen_file;
			_files[LOCAL_NODE]   = &_local_file;
			_files[REMOTE_NODE]  = &_remote_file;
		}

		~Ip_socket_dir()
		{
			_accept_file .dissolve_channels();
			_bind_file   .dissolve_channels();
			_connect_file.dissolve_channels();
			_data_file   .dissolve_channels();
			_peek_file   .dissolve_channels();
			_listen_file .dissolve_channels();
			_local_file  .dissolve_channels();
			_remote_file .dissolve_channels();

			genode_socket_release(&_sock);
			_parent.release(id);
		}

		/**************************
		 ** Socket_dir interface **
		 **************************/

		Protocol_dir &parent() override { return _parent; }

		genode_sockaddr &remote_addr() override { return _remote_addr; }

		char const *top_dir() override { return _parent.top_dir(); }

		Open_result
		open(File_system &fs, char const *path, Open_attr attr, Allocator &alloc) override
		{
			++path;

			if (strcmp(path, "accept_socket") == 0)
				return _accept_new_socket(alloc);

			for (Vfs_ip::File *f : _files) {
				if (f && strcmp(f->name(), path) == 0) {
					return *new (alloc)
						Vfs_ip::Ip_file_channel(alloc, { .writeable = attr.writeable }, f);
				}
			}

			/* error file */
			if (strcmp(path, _error_fs.name()) == 0) {
				/* add leading slash back to path */
				return _error_fs.open(path - 1, { }, alloc);
			}

			return _sockopt_fs.open(fs, path, attr, alloc);
		}

		void connect(bool) override { }

		void listen(bool v) override
		{
			_files[ACCEPT_SOCKET_NODE] = v ? &_accept_socket_file : nullptr;
		}

		bool _closed = false;

		void close()        override { _closed = true; }
		bool closed() const override { return _closed; }


		Errno socket_error(Errno const err) override
		{
			return _error_fs.socket_error(err);
		}

		/*************************
		 ** Directory interface **
		 *************************/

		Vfs_ip::Node *child(char const *name) override
		{
			for (Vfs_ip::File *n : _files)
				if (n && strcmp(n->name(), name) == 0)
					return n;

			if (strcmp(_error_fs.name(), name) == 0)
				return &_error_fs;

			/* check sockopts */
			return _sockopt_fs.child(name);
		}

		unsigned num_dirent() override { return _num_nodes() + 1; }

		Dir_channel::Read_dir_result read(Dir_channel::At at, Dirent_range_ptr const &dst) override
		{
			if (dst.num_entries < 1)
				return Dir_channel::Read_dir_error::DENIED;

			unsigned index = at.pos;

			Vfs_ip::Node *node = nullptr;
			for (Vfs_ip::File *n : _files) {
				if (n) {
					if (index == 0) {
						node = n;
						break;
					}
					--index;
				}
			}
			if (!node)
				return Dir_channel::Read_dir_end();

			dst.start[0] = {
				.type = Dirent_type::DEVICE,
				.rwx  = Node_rwx::rw(),
				.name = { node->name() } };

			return 1;
		}

		Ip_socket_dir(const Ip_socket_dir&) = delete;
		Ip_socket_dir operator=(const Ip_socket_dir&) = delete;
};


struct Vfs_ip::Ip_socket_file_channel : Vfs::File_channel
{
	Allocator &_alloc;

	Ip_socket_dir _dir;

	Ip_socket_file_channel(Allocator &alloc,
	                       Vfs::Env &env,
	                       Parent_fs &parent_fs,
	                       Protocol_dir &parent,
	                       genode_socket_handle &sock)
	:
		Vfs::File_channel({ }),
		_alloc(alloc), _dir(env, parent_fs, alloc, parent, sock)
	{ }

	bool read_ready() const override { return true; }

	Read_result read(At, Byte_range_ptr const &dst) override
	{
		return Format::snprintf(
			dst.start, dst.num_bytes, "%s/%s\n", _dir.parent().name(), _dir.name());
	}

	bool write_ready() const override { return false; }

	void destruct() override { destroy(_alloc, this); }
};


Genode::Vfs::Open_result
Vfs_ip::Ip_socket_dir::_accept_new_socket(Allocator &alloc)
{
	if (!_files[ACCEPT_SOCKET_NODE]) return Open_error::DENIED;

	Errno err;
	genode_socket_handle *new_sock = genode_socket_accept(&_sock, nullptr, &err);
	if (err != GENODE_ENONE) {
		error("accept socket failed");
		return Open_error::DENIED;
	}

	Open_error error = Open_error::DENIED;
	try {
		return *new (alloc)
			Vfs_ip::Ip_socket_file_channel(alloc, _env, _parent_fs, _parent, *new_sock);
	}
	catch (Out_of_ram)  { error = Open_error::OUT_OF_RAM;  }
	catch (Out_of_caps) { error = Open_error::OUT_OF_CAPS; }
	catch (...) { Genode::error("unhandled error during accept"); }

	genode_socket_release(new_sock);
	return error;
};


class Vfs_ip::Protocol_dir_impl : public Protocol_dir
{
	private:

		Vfs::Env    &_env;
		Parent_fs   &_parent_fs;
		Allocator   &_alloc;
		File_system &_parent;

		struct New_socket_file : Vfs_ip::File
		{
			New_socket_file() : Vfs_ip::File("new_socket") { }
		} _new_socket_file { };

		Type const _type;

		/**************************
		 ** Simple node registry **
		 **************************/

		enum { MAX_NODES = MAX_SOCKETS + 1 };
		Vfs_ip::Node *_nodes[MAX_NODES];

		unsigned _num_nodes()
		{
			unsigned n = 0;
			for (size_t i = 0; i < MAX_NODES; i++)
				n += (_nodes[i] != nullptr);
			return n;
		}

		Vfs_ip::Node **_unused_node()
		{
			for (size_t i = 0; i < MAX_NODES; i++)
				if (_nodes[i] == nullptr) return &_nodes[i];
			throw -1;
		}

		void _free_node(Vfs_ip::Node *node)
		{
			for (size_t i = 0; i < MAX_NODES; i++)
				if (_nodes[i] == node) {
					_nodes[i] = nullptr;
					break;
				}
		}

		bool _is_root(const char *path)
		{
			return (strcmp(path, "") == 0) || (strcmp(path, "/") == 0);
		}

		Open_result _open_new_socket(Allocator &alloc)
		{
			int type = (_type == Protocol_dir::TYPE_STREAM)
			         ? SOCK_STREAM : SOCK_DGRAM;

			Errno err;
			genode_socket_handle *sock = genode_socket(AF_INET, type, 0, &err);
			if (sock == nullptr) return Open_error::DENIED;

			/* XXX always allow UDP broadcast */
			if (type == SOCK_DGRAM) {
				int enable = 1;
				genode_socket_setsockopt(sock, GENODE_SOL_SOCKET, GENODE_SO_BROADCAST,
				                         &enable, sizeof(enable));
			}

			Open_error error = Open_error::DENIED;
			try {
				return *new (alloc)
					Vfs_ip::Ip_socket_file_channel(alloc, _env, _parent_fs, *this, *sock);
			}
			catch (Out_of_ram)  { error = Open_error::OUT_OF_RAM;  }
			catch (Out_of_caps) { error = Open_error::OUT_OF_CAPS; }
			catch (...) { Genode::error("unhandled error during _open_new_socket"); }

			genode_socket_release(sock);
			return error;
		}

	public:

		Protocol_dir_impl(Vfs::Env          &env,
		                  Parent_fs         &parent_fs,
		                  Allocator         &alloc,
		                  File_system       &parent,
		                  char        const *name,
		                  Protocol_dir::Type type)
		:
			Protocol_dir(name),
			_env(env), _parent_fs(parent_fs), _alloc(alloc), _parent(parent), _type(type)
		{
			for (size_t i = 0; i < MAX_NODES; i++) {
				_nodes[i] = nullptr;
			}

			_nodes[0] = &_new_socket_file;
		}

		~Protocol_dir_impl() { }

		Vfs_ip::Node *lookup(char const *path)
		{
			if (*path == '/') path++;
			if (*path == '\0') return this;

			char const *p = path;
			while (*++p && *p != '/');

			for (size_t i = 0; i < MAX_NODES; i++) {
				if (!_nodes[i]) continue;

				if (strcmp(_nodes[i]->name(), path, (p - path)) == 0) {
					Vfs_ip::Directory *dir = dynamic_cast<Directory *>(_nodes[i]);
					if (!dir) return _nodes[i];

					Socket_dir *socket = dynamic_cast<Socket_dir *>(_nodes[i]);
					if (socket && socket->closed())
						return nullptr;

					if (*p == '/') return dir->child(p+1);
					else           return dir;
				}
			}

			return nullptr;
		}

		Unlink_result unlink(char const *path)
		{
			Vfs_ip::Node *node = lookup(path);
			if (!node) return Unlink_result::DENIED;

			Vfs_ip::Directory *dir = dynamic_cast<Vfs_ip::Directory*>(node);
			if (!dir) return Unlink_result::DENIED;

			_free_node(node);

			destroy(&_alloc, dir);

			return Unlink_result::OK;
		}

		/****************************
		 ** Protocol_dir interface **
		 ****************************/

		char const *top_dir() override { return name(); }

		Type type() override { return _type; }

		Open_result open(File_system &fs, char const *path, Open_attr attr, Allocator &alloc) override
		{
			if (strcmp(path, "/new_socket") == 0) {
				if (attr.writeable) return Open_error::DENIED;
				return _open_new_socket(alloc);
			}

			path++;
			char const *p = path;
			while (*++p && *p != '/');

			for (size_t i = 1; i < MAX_NODES; i++) {
				if (!_nodes[i]) continue;
				if (strcmp(_nodes[i]->name(), path, (p - path)) == 0) {
					Vfs_ip::Directory *dir = dynamic_cast<Directory *>(_nodes[i]);
					if (dir) {
						path += (p - path);
						return dir->open(fs, path, attr, alloc);
					}
				}
			}

			return Open_error::DENIED;
		}

		unsigned adopt_socket(Socket_dir &dir) override
		{
			Vfs_ip::Node **node = _unused_node();
			if (!node) throw -1;

			unsigned long const id = ((unsigned char*)node - (unsigned char*)_nodes)/sizeof(*_nodes);

			*node = &dir;
			return unsigned(id);
		}

		void release(unsigned id) override
		{
			if (id < MAX_NODES)
				_nodes[id] = nullptr;
		}

		/*************************
		 ** Directory interface **
		 *************************/

		unsigned num_dirent() override { return _num_nodes(); }

		Dir_channel::Read_dir_result read(Dir_channel::At at, Dirent_range_ptr const &dst) override
		{
			if (dst.num_entries < 1)
				return Dir_channel::Read_dir_error::DENIED;

			unsigned index = at.pos;

			Vfs_ip::Node *node = nullptr;
			for (Vfs_ip::Node *n : _nodes) {
				if (n) {
					if (index == 0) {
						node = n;
						break;
					}
					--index;
				}
			}
			if (!node)
				return Dir_channel::Read_dir_end();

			Dirent_type const type = dynamic_cast<Vfs_ip::Directory*>(node)
			                       ? Dirent_type::DIRECTORY
			                       : Dirent_type::DEVICE;

			Node_rwx const rwx = (type == Dirent_type::DIRECTORY)
			                   ? Node_rwx::rwx()
			                   : Node_rwx::rw();

			dst.start[0] = {
				.type = type,
				.rwx  = rwx,
				.name = { node->name() } };

			return 1;
		}

		Vfs_ip::Node *child(char const *) override { return nullptr; }

		Protocol_dir_impl(const Protocol_dir_impl&) = delete;
		Protocol_dir_impl operator=(const Protocol_dir_impl&) = delete;
};


struct Vfs_ip::Ip_address_info
{
	genode_socket_info _info { };

	void update() { genode_socket_config_info(&_info); }
};


class Vfs_ip::Ip_address_file final : public Vfs_ip::File
{
	private:

		unsigned          &_numeric_address;
		Ip_address_info &_info;

	public:

		Ip_address_file(char const *name,
		                  unsigned &numeric_address,
		                  Ip_address_info &info)
		: Vfs_ip::File(name),
		  _numeric_address(numeric_address), _info(info) { }

		long read(Ip_file_channel &, Byte_range_ptr const &dst, file_size) override
		{
			_info.update();

			enum {
				MAX_ADDRESS_STRING_SIZE = sizeof("000.000.000.000\n")
			};

			String<MAX_ADDRESS_STRING_SIZE> address {
				Net::Ipv4_address(&_numeric_address)
			};

			size_t n = min(dst.num_bytes, strlen(address.string()));
			memcpy(dst.start, address.string(), n);
			if (n < dst.num_bytes)
				dst.start[n++] = '\n';

			return n;
		}
};


class Vfs_ip::Ip_link_state_file final : public Vfs_ip::File
{
	private:

		bool              &_numeric_link_state;
		Ip_address_info &_info;

	public:

		Ip_link_state_file(char const *name,
		                     bool &numeric_link_state,
		                     Ip_address_info &info)
		: Vfs_ip::File(name),
		  _numeric_link_state(numeric_link_state), _info(info) { }

		long read(Ip_file_channel &, Byte_range_ptr const &dst, file_size) override
		{
			_info.update();

			enum {
				MAX_LINK_STATE_STRING_SIZE = sizeof("down\n")
			};

			String<MAX_LINK_STATE_STRING_SIZE> link_state {
				_numeric_link_state ? "up" : "down"
			};

			size_t n = min(dst.num_bytes, strlen(link_state.string()));
			memcpy(dst.start, link_state.string(), n);
			if (n < dst.num_bytes)
				dst.start[n++] = '\n';

			return n;
		}
};


/*******************************
 ** Filesystem implementation **
 *******************************/

class Vfs_ip::Ip_file_system : public  Vfs::File_system,
                               public  Vfs_ip::Directory,
                               private Vfs_ip::Ip_address_info,
                               private Remote_io
{
	private:

		Vfs::Env        &_env;
		Parent_fs       &_parent_fs;
		Entrypoint      &_ep       { _env.env().ep() };
		Allocator       &_alloc    { _env.alloc()    };
		Vfs::Env::User  &_vfs_user { _env.user()     };
		Remote_io::Peer  _peer     { _env.deferred_wakeups(), *this };

		genode_socket_wakeup _wakeup_remote { };

		Protocol_dir_impl _tcp_dir {
			_env, _parent_fs, _alloc, *this, "tcp", Protocol_dir::TYPE_STREAM };
		Protocol_dir_impl _udp_dir {
			_env, _parent_fs, _alloc, *this, "udp", Protocol_dir::TYPE_DGRAM  };

		Ip_address_file    _address    { "address",    _info.ip_addr,    *this };
		Ip_address_file    _netmask    { "netmask",    _info.netmask,    *this };
		Ip_address_file    _gateway    { "gateway",    _info.gateway,    *this };
		Ip_address_file    _nameserver { "nameserver", _info.nameserver, *this };
		Ip_link_state_file _link_state { "link_state", _info.link_state, *this };

		Vfs_ip::Node *_lookup(char const *path)
		{
			if (*path == '/') path++;
			if (*path == '\0') return this;

			if (strcmp(path, "tcp", 3) == 0)
				return _tcp_dir.lookup(&path[3]);

			if (strcmp(path, "udp", 3) == 0)
				return _udp_dir.lookup(&path[3]);

			if (strcmp(path, _address.name(), strlen(_address.name()) + 1) == 0)
				return &_address;

			if (strcmp(path, _netmask.name(), strlen(_netmask.name()) + 1) == 0)
				return &_netmask;

			if (strcmp(path, _gateway.name(), strlen(_gateway.name()) + 1) == 0)
				return &_gateway;

			if (strcmp(path, _nameserver.name(), strlen(_nameserver.name()) + 1) == 0)
				return &_nameserver;

			if (strcmp(path, _link_state.name(), strlen(_link_state.name()) + 1) == 0)
				return &_link_state;

			return nullptr;
		}

		bool _is_root(const char *path)
		{
			return (strcmp(path, "") == 0) || (strcmp(path, "/") == 0);
		}

		/*
		 * trigger 'wakeup_remote_peer' when VFS goes idle
		 */
		void schedule_wakeup()
		{
			_vfs_user.wakeup_vfs_user();
			_peer.schedule_wakeup();
		}

		void wakeup_remote_peer() override
		{
			genode_socket_wakeup_remote();
		}

		static void _schedule_wakeup(void *data)
		{
			Ip_file_system *fs = static_cast<Ip_file_system *>(data);
			fs->schedule_wakeup();
		}

	public:

		Ip_file_system(Vfs::Env &env, Parent_fs &parent_fs, Genode::Node const &node)
		:
			File_system(Ident { node.type() }), Directory(""),
			_env(env), _parent_fs(parent_fs)
		{
			_wakeup_remote.data     = this;
			_wakeup_remote.callback = _schedule_wakeup;

			genode_socket_register_wakeup(&_wakeup_remote);
		}

		~Ip_file_system() { }

		bool matches(Genode::Node const &node) const override
		{
			/* accept updated attributes w/o re-constructing the file system */
			return node.type() == _ident.string;
		}

		/***************************
		 ** File_system interface **
		 ***************************/

		Progress update(Genode::Node const &config, File_system::Factory &) override
		{
			using Addr = String<16>;

			unsigned const mtu = config.attribute_value("mtu", 0U);
			if (mtu) {
				log("Setting MTU to ", mtu);
				genode_socket_configure_mtu(mtu);
			} else {
				genode_socket_configure_mtu(0);
			}

			if (config.attribute_value("dhcp", false)) {
				log("Using DHCP for interface configuration.");
				genode_socket_config address_config = { .dhcp = true };
				genode_socket_config_address(&address_config);
				return PROGRESSED;
			}

			Addr ip_addr    = config.attribute_value("ip_addr", Addr());
			Addr netmask    = config.attribute_value("netmask", Addr());
			Addr gateway    = config.attribute_value("gateway", Addr());
			Addr nameserver = config.attribute_value("nameserver", Addr());

			if (ip_addr == "") {
				warning("Missing \"ip_addr\" attribute. Ignoring network interface config.");
				return PROGRESSED;
			} else if (netmask == "") {
				warning("Missing \"netmask\" attribute. Ignoring network interface config.");
				return PROGRESSED;
			}

			log("static network interface: ip_addr=",ip_addr," netmask=",netmask);

			genode_socket_config address_config = {
				.dhcp       = false,
				.ip_addr    = ip_addr.string(),
				.netmask    = netmask.string(),
				.gateway    = gateway.string(),
				.nameserver = nameserver.string(),
			};

			genode_socket_config_address(&address_config);

			return PROGRESSED;
		}


		/*************************
		 ** Directory interface **
		 *************************/

		unsigned num_dirent() override { return 7; }

		using Open_attr = Vfs::File_system::Open_attr;

		Open_result open(File_system &, char const *, Open_attr, Allocator &) override
		{
			return Open_error::DENIED;
		}

		Dir_channel::Read_dir_result read(Dir_channel::At at, Dirent_range_ptr const &dst) override
		{
			if (dst.num_entries < 1)
				return Dir_channel::Read_dir_error::DENIED;

			unsigned const index = at.pos;

			struct Entry
			{
				Dirent_type type;
				char const *name;
			};

			enum { NUM_ENTRIES = 7U };
			static Entry const entries[NUM_ENTRIES] = {
				{ Dirent_type::DIRECTORY, "tcp" },
				{ Dirent_type::DIRECTORY, "udp" },
				{ Dirent_type::DEVICE,    "address" },
				{ Dirent_type::DEVICE,    "netmask" },
				{ Dirent_type::DEVICE,    "gateway" },
				{ Dirent_type::DEVICE,    "nameserver" },
				{ Dirent_type::DEVICE,    "link_state" },
			};

			if (index >= NUM_ENTRIES)
				return -1;

			Entry const &entry = entries[index];

			dst.start[0] = {
				.type = entry.type,
				.rwx  = entry.type == Dirent_type::DIRECTORY
				      ? Node_rwx::rwx() : Node_rwx::rw(),
				.name = { entry.name }
			};
			return 1;
		}

		Vfs_ip::Node *child(char const *) override { return nullptr; }

		Stat_result stat(char const *path, Stat &out) override
		{
			Node *node = _lookup(path);
			if (!node) return Stat_result::DENIED;

			out = { };

			if (dynamic_cast<Directory*>(node)) {
				out.type = Dirent_type::DIRECTORY;
				out.rwx  = Node_rwx::rwx();
				out.size = 1;
				return Stat_result::OK;
			}

			if (dynamic_cast<Ip_data_file*>(node)) {
				out.type = Dirent_type::FILE;
				out.rwx  = Node_rwx::rw();
				out.size = 0;
				return Stat_result::OK;
			}

			if (dynamic_cast<Ip_peek_file*>(node)) {
				out.type = Dirent_type::FILE;
				out.rwx  = Node_rwx::rw();
				out.size = 0;
				return Stat_result::OK;
			}

			if (dynamic_cast<Vfs_ip::File*>(node)) {
				out.type = Dirent_type::DEVICE;
				out.rwx  = Node_rwx::rw();
				out.size = 0x1000;  /* there may be something to read */
				return Stat_result::OK;
			}

			return Stat_result::DENIED;
		}

		unsigned num_dirent(char const *path) override
		{
			if (_is_root(path)) return num_dirent();

			Vfs_ip::Node *node = _lookup(path);
			if (!node) return 0;

			Vfs_ip::Directory *dir = dynamic_cast<Vfs_ip::Directory*>(node);
			if (!dir) return 0;

			return dir->num_dirent();
		}

		bool directory(char const *path) override
		{
			Vfs_ip::Node *node = _lookup(path);
			return node ? dynamic_cast<Vfs_ip::Directory *>(node) : 0;
		}

		bool dir_entry_exists(char const *path) override
		{
			Vfs_ip::Node *node = _lookup(path);
			return node != nullptr;
		}

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
		{
			try {
				if (strcmp(path, "/tcp", 4) == 0)
					return _tcp_dir.open(*this, &path[4], attr, alloc);
				if (strcmp(path, "/udp", 4) == 0)
					return _udp_dir.open(*this, &path[4], attr, alloc);

				Vfs_ip::Node *node = _lookup(path);
				if (!node) return Open_error::DENIED;

				Vfs_ip::File *file = dynamic_cast<Vfs_ip::File*>(node);
				if (file)
					return *new (alloc)
						Vfs_ip::Ip_file_channel(alloc, { .writeable = attr.writeable }, file);
				return Open_error::DENIED;
			}
			catch (Out_of_ram ) { return Open_error::OUT_OF_RAM;  }
			catch (Out_of_caps) { return Open_error::OUT_OF_CAPS; }
		}

		Opendir_result opendir(char const *path, Allocator &alloc) override
		{
			Vfs_ip::Node *node = _lookup(path);

			if (!node) return Opendir_error::DENIED;

			Vfs_ip::Directory *dir = dynamic_cast<Vfs_ip::Directory*>(node);
			if (dir)
				return *new (alloc) Vfs_ip::Ip_dir_channel(alloc, *dir);

			return Opendir_error::DENIED;
		}

		Unlink_result unlink(char const *path) override
		{
			if (*path == '/') path++;

			if (strcmp(path, "tcp", 3) == 0)
				return _tcp_dir.unlink(&path[3]);
			if (strcmp(path, "udp", 3) == 0)
				return _udp_dir.unlink(&path[3]);
			return Unlink_result::DENIED;
		}
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	static Vfs_ip::Ip_file_channel::Fifo read_ready_waiters;

	_read_ready_waiters_ptr = &read_ready_waiters;

	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		/* wakup user task */
		static void socket_progress(void *data)
		{
			Vfs::Env *env = static_cast<Vfs::Env *>(data);
			env->user().wakeup_vfs_user();
			poll_all();
		}

		struct genode_socket_io_progress io_progress { };

		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &parent_fs, Node const &config) override
		{
			io_progress.data = &env;
			io_progress.callback = socket_progress;

			using Label = String<Session_label::capacity()>;

			if (genode_socket_init(genode_env_ptr(env.env()), &io_progress,
			                       config.attribute_value("label", Label("")).string())) {
				auto &fs = *new (env.alloc()) Vfs_ip::Ip_file_system(env, parent_fs, config);
				return { *this, { fs } };
			}

			error("vfs_ip: socket init failed");
			return Error::DENIED;
		}

		void _free(Instance &) override { };
	};

	static Factory factory;
	return &factory;
}
