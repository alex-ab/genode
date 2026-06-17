/*
 * \brief  Libc pseudo plugin for socket fs
 * \author Christian Helmuth
 * \author Christian Prochaska
 * \author Norman Feske
 * \author Emery Hemingway
 * \date   2015-06-23
 */

/*
 * Copyright (C) 2015-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/env.h>
#include <base/log.h>
#include <vfs/types.h>
#include <util/dictionary.h>
#include <util/string.h>
#include <libc/allocator.h>

/* libc includes */
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>
#include <net/if.h>

/* libc-internal includes */
#include <internal/kernel.h>
#include <internal/socket.h>
#include <internal/file.h>
#include <internal/errno.h>
#include <internal/init.h>
#include <internal/socket_errno.h>
#include <internal/pthread.h>


static Genode::Allocator  *_kernel_heap_ptr;
static Libc::Monitor      *_monitor_ptr;
static Libc::Config const *_config_ptr;


void Libc::init_socket(Genode::Allocator &kernel_heap, Monitor &monitor,
                       Fds &fds, Config const &config)
{
	_kernel_heap_ptr = &kernel_heap;
	_monitor_ptr     = &monitor;
	_fds_ptr         = &fds;
	_config_ptr      = &config;
}


static Libc::Monitor &monitor()
{
	struct Missing_call_of_init_socket : Genode::Exception { };
	if (!_monitor_ptr)
		throw Missing_call_of_init_socket();
	return *_monitor_ptr;
}


static Genode::Allocator &kernel_heap()
{
	struct Missing_call_of_init_socket : Genode::Exception { };
	if (!_kernel_heap_ptr)
		throw Missing_call_of_init_socket();
	return *_kernel_heap_ptr;
}


namespace { using Fn = Libc::Monitor::Function_result; }


struct Libc::Socket : Noncopyable
{
	public:

		struct Absolute_path : Vfs::Absolute_path
		{
			Absolute_path() { }

			Absolute_path(char const *path, char const *pwd = 0)
			:
				Vfs::Absolute_path(path, pwd)
			{
				remove_trailing('\n');
			}
		};

		template <int> class String;
		using Host_string = String<NI_MAXHOST>;
		using Port_string = String<NI_MAXSERV>;
		struct Sockaddr_string;

		struct New_socket_failed : Exception { };
		struct Address_conversion_failed : Exception { };

		struct Sockaddr_functor;
		struct Remote_functor;
		struct Local_functor;

		enum { MAX_CONTROL_PATH_LEN = 16 };

		enum Proto { TCP, UDP };

		enum State { UNCONNECTED, ACCEPT_ONLY, CONNECTING, CONNECTED, CONNECT_ABORTED };

		/* TODO remove */
		struct Inaccessible { }; /* exception */

	private:

		/*
		 * This is the file descriptor representing the socket and must be held
		 * open until the socket is closed. The file content is the location of
		 * the socket in the socket FS.
		 */
		int const _handle_fd;
		int       _fd_flags { 0 };

		Absolute_path _read_socket_path()
		{
			Absolute_path path;
			int const n = read(_handle_fd, path.base(),
			                   Absolute_path::capacity()-1);
			if (n == -1 || !n || n >= (int)Absolute_path::capacity() - 1)
				throw New_socket_failed();
			*(path.base()+n) = '\0';

			return path;
		}

		Absolute_path const _path {
			_read_socket_path().base(), _config_ptr->socket.string() };

		enum Fd { DATA,   PEEK,  CONNECT, BIND,  LISTEN,
		          ACCEPT, LOCAL, REMOTE,  ERROR, MAX };

		struct
		{
			char const      *name;
			int              num;
			File_descriptor *file;
		} _fd[Fd::MAX] = {
			{ "data",    -1, nullptr }, { "peek",   -1, nullptr },
			{ "connect", -1, nullptr }, { "bind",   -1, nullptr },
			{ "listen",  -1, nullptr }, { "accept", -1, nullptr },
			{ "local",   -1, nullptr }, { "remote", -1, nullptr },
			{ "error",   -1, nullptr },
		};


		Proto const _proto;

		State _state { UNCONNECTED };

		template <typename FUNC>
		void _fd_apply(FUNC const &fn)
		{
			if (_fd[DATA].num    != -1) fn(_fd[DATA].num);
			if (_fd[CONNECT].num != -1) fn(_fd[CONNECT].num);
			if (_fd[ACCEPT].num  != -1) fn(_fd[ACCEPT].num);
		}

		void _init_fd(Fd type, int flags)
		{
			Absolute_path file(_fd[type].name, _path.base());
			int const fd = open(file.base(), flags|_fd_flags);
			if (fd == -1) {
				error(__func__, ": ", _fd[type].name,
				      " file not accessible at ", file,
				      " errno=", errno);
				throw New_socket_failed();
			}
			_fd[type].num  = fd;
			_fd[type].file = nullptr;
			fds().with_space([&] (Fds::Space &space) {
				space.apply<File_descriptor>({ addr_t(fd) },
					[&] (File_descriptor &fd) { _fd[type].file = &fd; },
					[&] { });
			});
		}

		bool _fd_read_ready(Fd type)
		{
			if (!_fd[type].file) return false;

			bool ret = false;
			auto fn = [&] {
				ret = Libc::read_ready_from_kernel(*_fd[type].file);
				return Fn::COMPLETE;
			};

			if (Libc::Kernel::kernel().main_context() && Libc::Kernel::kernel().main_suspended()) {
				fn();
			} else {
				monitor().monitor(fn);
			}

			return ret;
		}

		bool _fd_write_ready(Fd type)
		{
			if (_fd[type].file)
				return Libc::write_ready_from_kernel(*_fd[type].file);
			else
				return false;
		}

	public:

		Socket(Proto proto, int handle_fd)
		: _handle_fd(handle_fd), _proto(proto)
		{
			_init_fd(Fd::DATA,    O_RDWR);
			_init_fd(Fd::PEEK,    O_RDONLY);
			_init_fd(Fd::CONNECT, O_RDWR);
			_init_fd(Fd::BIND,    O_WRONLY);
			_init_fd(Fd::LISTEN,  O_WRONLY);
			_init_fd(Fd::ACCEPT,  O_RDONLY);
			_init_fd(Fd::LOCAL,   O_RDWR);
			_init_fd(Fd::REMOTE,  O_RDWR);
			_init_fd(Fd::ERROR,   O_RDONLY);
		}

		~Socket()
		{
			for (unsigned i = 0; i < Fd::MAX; ++i) {
				::close(_fd[i].num);
				_fd[i].num = -1;
				_fd[i].file = nullptr;
			}
			::close(_handle_fd);
		}

		Absolute_path path() const { return _path; }

		Proto proto() const { return _proto; }

		int fd_flags() const { return _fd_flags; }
		void fd_flags(int flags)
		{
			_fd_flags = flags;
			_fd_apply([flags] (int fd) { fcntl(fd, F_SETFL, flags); });
		}

		int data_fd()    { return _fd[Fd::DATA].num; }
		int peek_fd()    { return _fd[Fd::PEEK].num; }
		int connect_fd() { return _fd[Fd::CONNECT].num; }
		int bind_fd()    { return _fd[Fd::BIND].num; }
		int listen_fd()  { return _fd[Fd::LISTEN].num; }
		int accept_fd()  { return _fd[Fd::ACCEPT].num; }
		int local_fd()   { return _fd[Fd::LOCAL].num; }
		int remote_fd()  { return _fd[Fd::REMOTE].num; }

		/* request the appropriate fd to ensure the file is open */
		bool connect_read_ready() { return _fd_read_ready(Fd::CONNECT); }
		bool data_read_ready()    { return _fd_read_ready(Fd::DATA); }
		bool accept_read_ready()  { return _fd_read_ready(Fd::ACCEPT); }

		bool local_read_ready_from_kernel()
		{
			if (!_fd[Fd::LOCAL].file) return false;

			return  Libc::read_ready_from_kernel(*_fd[Fd::LOCAL].file);
		}

		bool remote_read_ready_from_kernel()
		{
			if (!_fd[Fd::REMOTE].file) return false;

			return  Libc::read_ready_from_kernel(*_fd[Fd::REMOTE].file);
		}

		void state(State state) { _state = state; }
		State state() const     { return _state; }

		bool read_ready()
		{
			return (_state == ACCEPT_ONLY) ? accept_read_ready() : data_read_ready();
		}

		bool write_ready()
		{
			if (_state == CONNECTING)
				return connect_read_ready();

			return _fd_write_ready(Fd::DATA);
		}

		/*
		 * Read the connect status from the connect file and return 0 if connected
		 * or -1 with errno set to the error code.
		 */
		int read_connect_status()
		{
			char connect_status[32] = { 0 };
			ssize_t connect_status_len;

			connect_status_len = read(connect_fd(), connect_status,
			                          sizeof(connect_status));

			if (connect_status_len <= 0) {
				error("socket: reading from the connect file failed");
				return -1;
			}

			using ::strcmp;

			if (strcmp(connect_status, "connected") == 0)
				return 0;

			if (strcmp(connect_status, "connection refused") == 0)
				return Errno(ECONNREFUSED);

			if (strcmp(connect_status, "not connected") == 0)
				return Errno(ENOTCONN);

			if (strcmp(connect_status, "no route to host") == 0)
				return Errno(EHOSTUNREACH);

			error("socket: unhandled connection state");
			return Errno(ECONNREFUSED);
		}

		struct Sockopt;
		using Sockopt_dict = Genode::Dictionary<Sockopt, int>;

		struct Sockopt : Sockopt_dict::Element
		{
			char const *_file;

			Sockopt(Sockopt_dict &dict, int opt, char const *file)
			: Sockopt_dict::Element(dict, opt), _file(file) { }

			char const *base() const { return _file; }
		};

		Sockopt_dict _sockopt_dict { };

		Sockopt _keepalive { _sockopt_dict, SO_KEEPALIVE ,"so_keepalive" };
		Sockopt _reuseaddr { _sockopt_dict, SO_REUSEADDR ,"so_reuseaddr" };

		Sockopt _tcp_keepcnt   { _sockopt_dict, TCP_KEEPCNT,  "tcp_keepcnt"   };
		Sockopt _tcp_keepidle  { _sockopt_dict, TCP_KEEPIDLE, "tcp_keepidle"  };
		Sockopt _tcp_keepintvl { _sockopt_dict, TCP_KEEPINTVL,"tcp_keepintvl" };

		template <typename FN>
		int with_sockopt_fd(int optname, FN const &fn)
		{
			Absolute_path dir { "sockopts", _path.base() };

			int err = ENOPROTOOPT;
			_sockopt_dict.with_element(optname, [&](Sockopt &opt) {

				Absolute_path file { opt.base(), dir.base() };

				int fd = ::open(file.base(), O_RDWR);
				if (fd < 0) return;

				ssize_t n = fn(fd);

				::close(fd);

				if (n > 0) err = 0;
			}, []{ });

			return err;
		}

		/* set errno to current socket error */
		void socket_error()
		{
			char buf[64];

			ssize_t bytes = read(_fd[ERROR].num, buf, sizeof(buf));

			Node node { Const_byte_range_ptr { buf, size_t(bytes) } };
			int genode_errno = node.attribute_value("value", -1);

			errno = socket_errno(genode_errno);
		}
};


struct Libc::Socket::Sockaddr_functor
{
	Socket &socket;
	bool const nonblocking;

	Sockaddr_functor(Socket &socket, bool nonblocking)
	: socket(socket), nonblocking(nonblocking) { }

	virtual bool read_ready_from_kernel() = 0;
	virtual int fd() = 0;
};


struct Libc::Socket::Remote_functor : Sockaddr_functor
{
	Remote_functor(Socket &socket, bool nonblocking)
	: Sockaddr_functor(socket, nonblocking) { }

	bool read_ready_from_kernel() override {
		return socket.remote_read_ready_from_kernel(); }

	int fd() override { return socket.remote_fd(); }
};


struct Libc::Socket::Local_functor : Sockaddr_functor
{
	Local_functor(Socket &socket, bool nonblocking)
	: Sockaddr_functor(socket, nonblocking) { }

	bool read_ready_from_kernel() override {
		return socket.local_read_ready_from_kernel(); }

	int fd() override { return socket.local_fd(); }
};


template <int CAPACITY> class Libc::Socket::String
{
	private:

		char _buf[CAPACITY] { 0 };

	public:

		String() { }

		constexpr size_t capacity() { return CAPACITY; }

		char const * base() const { return _buf; }
		char       * base()       { return _buf; }

		void terminate(size_t at) { _buf[at] = 0; }

		void remove_trailing_newline()
		{
			int i = 0;
			while (_buf[i] && _buf[i + 1]) i++;

			if (i > 0 && _buf[i] == '\n')
				_buf[i] = 0;
		}
};


/*
 * Both NI_MAXHOST and NI_MAXSERV include the terminating 0, which allows
 * use to put ':' between host and port on concatenation.
 */
struct Libc::Socket::Sockaddr_string : String<NI_MAXHOST + NI_MAXSERV>
{
	Sockaddr_string() { stpcpy(this->base(), ";0"); }

	Sockaddr_string(Host_string const &host, Port_string const &port)
	{
		char *b = base();
		b = stpcpy(b, host.base());
		b = stpcpy(b, ":");
		b = stpcpy(b, port.base());
	}

	Host_string host() const
	{
		Host_string host;

		Genode::copy_cstring(host.base(), base(), host.capacity());
		char *at = strstr(host.base(), ":");
		if (!at)
			throw Address_conversion_failed();
		*at = 0;

		return host;
	}

	Port_string port() const
	{
		Port_string port;

		char *at = strstr(base(), ":");
		if (!at)
			throw Address_conversion_failed();

		Genode::copy_cstring(port.base(), ++at, port.capacity());

		return port;
	}
};


using namespace Libc;


static Libc::Socket::Port_string port_string(sockaddr_in const &addr)
{
	Libc::Socket::Port_string port;

	if (getnameinfo((sockaddr *)&addr, sizeof(addr),
	                nullptr, 0, /* no host conversion */
	                port.base(), port.capacity(),
	                NI_NUMERICHOST | NI_NUMERICSERV) != 0)
		throw Libc::Socket::Address_conversion_failed();

	return port;
}


static Libc::Socket::Host_string host_string(sockaddr_in const &addr)
{
	Libc::Socket::Host_string host;

	if (getnameinfo((sockaddr *)&addr, sizeof(addr),
	                host.base(), host.capacity(),
	                nullptr, 0, /* no port conversion */
	                NI_NUMERICHOST | NI_NUMERICSERV) != 0)
		throw Libc::Socket::Address_conversion_failed();

	return host;
}


static sockaddr_in sockaddr_in_struct(Libc::Socket::Host_string const &host,
                                      Libc::Socket::Port_string const &port)
{
	addrinfo hints;
	addrinfo *info = nullptr;

	::bzero(&hints, sizeof(hints));
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

	if (getaddrinfo(host.base(), port.base(), &hints, &info))
		throw Libc::Socket::Address_conversion_failed();

	sockaddr_in addr = *(sockaddr_in*)info->ai_addr;

	freeaddrinfo(info);

	return addr;
}


static int read_sockaddr_in(Libc::Socket::Sockaddr_functor &func,
                            struct sockaddr_in *addr, socklen_t *addrlen)
{
	if (!addr)                     return Errno(EFAULT);
	if (!addrlen || *addrlen <= 0) return Errno(EINVAL);

	if (!func.nonblocking) {
		monitor().monitor([&] {
			return func.read_ready_from_kernel() ? Fn::COMPLETE : Fn::INCOMPLETE;
		});
	}

	Socket::Sockaddr_string addr_string;
	int const n = read(func.fd(), addr_string.base(), addr_string.capacity() - 1);

	if (n == -1) return Errno(errno);
	/* 0 return value means "no packet resp. not connected" */
	if (!n)
		switch (func.socket.proto()) {
		case Socket::Proto::UDP: return Errno(EAGAIN);
		case Socket::Proto::TCP: return Errno(ENOTCONN);
		}
	if (n >= (int)addr_string.capacity() - 1) return Errno(EINVAL);

	addr_string.terminate(n);
	addr_string.remove_trailing_newline();

	try {
		/* convert the address but do not exceed the caller's buffer */
		sockaddr_in saddr = sockaddr_in_struct(addr_string.host(), addr_string.port());
		::memcpy(addr, &saddr, *addrlen);
		*addrlen = sizeof(saddr);

		return 0;
	} catch (Libc::Socket::Address_conversion_failed) {
		warning("IP address conversion failed");
		return Errno(ENOBUFS);
	}
}


/***********************
 ** Address functions **
 ***********************/

int Libc::socket_getpeername(Socket &socket, sockaddr *addr, socklen_t *addrlen)
{
	switch (socket.proto()) {
	case Socket::Proto::UDP: return Errno(ENOTCONN);
	case Socket::Proto::TCP:
		{
			Socket::Remote_functor func(socket, false);
			return read_sockaddr_in(func, (sockaddr_in *)addr, addrlen);
		}
	}

	return 0;
}


int Libc::socket_getsockname(Socket &socket, sockaddr *addr, socklen_t *addrlen)
{
	Socket::Local_functor func(socket, false);
	return read_sockaddr_in(func, (sockaddr_in *)addr, addrlen);
}


/**************************
 ** Socket transport API **
 **************************/

Libc::Accept_result
Libc::socket_accept(Socket &listen_socket, sockaddr *addr, socklen_t *addrlen)
{
	/* TODO EOPNOTSUPP - no SOCK_STREAM */
	/* TODO ECONNABORTED */

	char accept_buf[Socket::MAX_CONTROL_PATH_LEN];
	{
		int n = 0;
		/* XXX currently reading accept may return without new connection */
		do {
			n = read(listen_socket.accept_fd(), accept_buf, sizeof(accept_buf));
		} while (n == 0);

		if (n == -1 && errno == EAGAIN)
			return Errno { EAGAIN };

		if (n == -1)
			return Errno { EINVAL };
	}

	Socket::Absolute_path path { listen_socket.path() };
	path.append("/accept_socket");

	int handle_fd = ::open(path.base(), O_RDONLY);
	if (handle_fd < 0) {
		error("failed to open accept socket at ", path);
		return Errno { EACCES };
	}

	try {
		Socket &accept_socket = *new (kernel_heap()) Socket(listen_socket.proto(), handle_fd);

		if (addr && addrlen) {
			Socket::Remote_functor func(accept_socket, false);
			int ret = read_sockaddr_in(func, (sockaddr_in *)addr, addrlen);
			if (ret == -1) {
				destroy(kernel_heap(), &accept_socket);
				return Errno { ret };
			}
		}

		/* inherit the O_NONBLOCK flag if set */
		accept_socket.fd_flags(listen_socket.fd_flags());

		return accept_socket;

	} catch (Socket::New_socket_failed) {
		close(handle_fd);
		return Errno { ENFILE };
	}
}


int Libc::socket_bind(Socket &socket, sockaddr const *addr, socklen_t addrlen)
{
	if (!addr) return Errno(EFAULT);

	if (addr->sa_family != AF_INET) {
		error(__func__, ": family ", addr->sa_family, " not supported");
		return Errno(EAFNOSUPPORT);
	}

	Socket::Sockaddr_string addr_string;

	try {
		addr_string = { host_string(*(sockaddr_in *)addr),
		                port_string(*(sockaddr_in *)addr) };
	}
	catch (Socket::Address_conversion_failed) { return Errno(EINVAL); }

	try {
		int const len = ::strlen(addr_string.base());
		int const n   = write(socket.bind_fd(), addr_string.base(), len);

		if (n != len) {
			socket.socket_error();
			return -1;
		}

		/* sync to block for write completion */
		return fsync(socket.bind_fd());
	} catch (Socket::Inaccessible) {
		return Errno(EINVAL);
	}
}


int Libc::socket_try_connect(Socket &socket, sockaddr const *addr, socklen_t addrlen)
{
	if (!addr) return Errno(EFAULT);

	switch (addr->sa_family) {
	case AF_UNSPEC:
		{
			if (socket.state() != Socket::CONNECTED)
				return 0;

			Socket::Sockaddr_string addr_string { };
			int const len = ::strlen(addr_string.base());
			int const n   = write(socket.connect_fd(), addr_string.base(), len);

			if (n != len) {
				socket.socket_error();
				return -1;
			}

			socket.state(Socket::UNCONNECTED);

			return 0;
		}
	case AF_INET:
		break;
	default:
		return Errno(EAFNOSUPPORT);
	}

	switch (socket.state()) {
	case Socket::UNCONNECTED:
		{
			Socket::Sockaddr_string addr_string;
			try {
				addr_string = { host_string(*(sockaddr_in const *)addr),
				                port_string(*(sockaddr_in const *)addr) };
			}
			catch (Socket::Address_conversion_failed) { return Errno(EINVAL); }

			socket.state(Socket::CONNECTING);

			int const len = ::strlen(addr_string.base());
			int const n   = write(socket.connect_fd(), addr_string.base(), len);

			if (n != len) {
				warning("socket_try_connect: write to connect_fd failed");
				socket.socket_error();
				return -1;
			}
			return Errno(EINPROGRESS);
		}
		break;
	case Socket::CONNECTING:
		{
			if (!socket.connect_read_ready())
				return Errno(EALREADY);

			int connect_status = socket.read_connect_status();

			if (connect_status == 0)
				socket.state(Socket::CONNECTED);
			else
				socket.state(Socket::CONNECT_ABORTED);

			/* errno was set by socket.read_connect_status() */
			return connect_status;
		}
	case Socket::ACCEPT_ONLY:     return Errno(EINVAL);
	case Socket::CONNECTED:       return Errno(EISCONN);
	case Socket::CONNECT_ABORTED: return Errno(ECONNABORTED);
	}

	return Errno(ECONNREFUSED);
}


int Libc::socket_connect_timed_out(Socket &socket)
{
	socket.state(Socket::CONNECT_ABORTED);
	return Errno(ETIMEDOUT);
}


int Libc::socket_listen(Socket &socket, int backlog)
{
	char buf[Socket::MAX_CONTROL_PATH_LEN];
	int const len = ::snprintf(buf, sizeof(buf), "%d", backlog);
	int const n   = write(socket.listen_fd(), buf, len);
	if (n != len) {
		socket.socket_error();
		return -1;
	}

	/* sync to block for write completion */
	int const res = fsync(socket.listen_fd());
	if (res != 0) return res;

	socket.state(Socket::ACCEPT_ONLY);
	return 0;
}


/*
 * In case a non-blocking socket returns EAGAIN, make sure wakeup_remote_peers
 * triggers in order send low-level packet-stream signals.  This is necessary in
 * cases where, for example, a spinning non-blocking read waits for data that
 * has not been sent because of deferred wakeup not being triggered yet.
 */
static void handle_wakeup_remote_peers(Socket &socket)
{
	if (errno == EAGAIN && socket.fd_flags() & O_NONBLOCK)
		Libc::Kernel::kernel().wakeup_remote_peers();
}


static ssize_t do_recvfrom(Socket &socket,
                           void *buf, ::size_t const len, int const flags,
                           struct sockaddr *src_addr, socklen_t *src_addrlen)
{
	if (!buf) return Errno(EFAULT);
	if (!len) return Errno(EINVAL);

	if (src_addr) {
		Socket::Remote_functor func(socket, socket.fd_flags() & O_NONBLOCK);
		int const res = read_sockaddr_in(func, (sockaddr_in *)src_addr, src_addrlen);
		if (res < 0) return res;
	}

	int data_fd = flags & MSG_PEEK ? socket.peek_fd() : socket.data_fd();

	try {
		if (lseek(data_fd, 0, SEEK_SET) != 0)
			return Errno(EINVAL);

		size_t out_sum = 0;

		do {
			ssize_t const result = read(data_fd,
			                            (char *)buf + out_sum,
			                            len - out_sum);

			if (result <= 0) { /* eof & error */
				if (out_sum)
					return out_sum;

				/* update errno unless VFS-plugin returned EAGAIN */
				if (errno != EAGAIN)
					socket.socket_error();

				if (result < 0) handle_wakeup_remote_peers(socket);

				return result;
			}

			out_sum += result;
		} while ((flags & MSG_WAITALL) &&
		         (out_sum < len));

		return out_sum;
	} catch (Socket::Inaccessible) {
		return Errno(EINVAL);
	}
}


ssize_t Libc::socket_recvfrom(Socket &socket, void *buf, ::size_t len, int flags,
                              sockaddr *src_addr, socklen_t *src_addrlen)
{
	return do_recvfrom(socket, buf, len, flags, src_addr, src_addrlen);
}


ssize_t Libc::socket_recv(Socket &socket, void *buf, ::size_t len, int flags)
{
	/* identical to recvfrom() with a NULL src_addr argument */
	return socket_recvfrom(socket, buf, len, flags, nullptr, nullptr);
}


ssize_t Libc::socket_recvmsg(Socket &socket, msghdr *msg, int flags)
{
	/* TODO just a simple implementation that handles the easy cases */
	size_t numberOfBytes = 0;
	char *data = nullptr;
	size_t length = 0;
	char *buffer;
	ssize_t res;
	size_t amount;
	socklen_t client_address_len;

	/* iterate over all msg_iov to get the number of bytes that have to be read. */
	for (int i = 0; i < msg->msg_iovlen; i++) {
		numberOfBytes += msg->msg_iov[i].iov_len;
		/*
		 * As an optimization, we set the initial values of DATA and LEN from
		 * the first non-empty iovec. This kicks-in in the case where the whole
		 * packet fits into the first iovec buffer.
		 */
		if (data == nullptr && msg->msg_iov[i].iov_len > 0) {
			data = (char*)msg->msg_iov[i].iov_base;
			length = msg->msg_iov[i].iov_len;
		}
	}

	buffer = data;

	struct sockaddr_in client_address;
	client_address_len = sizeof (client_address);

	/* do socket communication */
	res = socket_recvfrom(socket, buffer, length, flags,
	                      (struct sockaddr *) &client_address,
	                      &client_address_len);

	if(res < 0) {
		return res;
	}

	/* copy client address to msg_name */
	if (msg->msg_name != nullptr && client_address_len > 0) {
		if (msg->msg_namelen > client_address_len) {
			msg->msg_namelen = client_address_len;
		}

		::memcpy (msg->msg_name, &client_address, msg->msg_namelen);
	} else if (msg->msg_name != nullptr) {
		msg->msg_namelen = 0;
	}

	/* handle payload */
	if (buffer == data) {
		buffer += length;
	} else {
		amount = length;
		buffer = data;
		for (int i = 0; i < msg->msg_iovlen; i++) {
#define min(a, b)        ((a) > (b) ? (b) : (a))
			size_t copy = min (msg->msg_iov[i].iov_len, amount);
			::memcpy (msg->msg_iov[i].iov_base, buffer, copy);
			buffer += copy;
			amount -= copy;
			if (length == 0)
				break;
		}

		Libc::Allocator alloc { };
		destroy(alloc, data);
	}

	/* handle control data, not supported yet */
	msg->msg_controllen = 0;

	return res;
}


static ssize_t do_sendto(Socket &socket,
                         void const *buf, ::size_t len, int flags,
                         sockaddr const *dest_addr, socklen_t dest_addrlen)
{
	if (!buf) return Errno(EFAULT);
	if (!len) return Errno(EINVAL);

	/* TODO ENOTCONN, EISCONN, EDESTADDRREQ */

	try {
		if (dest_addr && socket.proto() == Socket::Proto::UDP) {
			try {
				Socket::Sockaddr_string addr_string(host_string(*(sockaddr_in const *)dest_addr),
				                                    port_string(*(sockaddr_in const *)dest_addr));

				int const len = ::strlen(addr_string.base());
				int const n   = write(socket.remote_fd(), addr_string.base(), len);
				if (n != len) return Errno(EIO);
			}
			catch (Socket::Address_conversion_failed) { return Errno(EINVAL); }
		}

		lseek(socket.data_fd(), 0, 0);
		ssize_t out_len = write(socket.data_fd(), buf, len);

		/* update errno unless VFS-plugin returned EAGAIN */
		if (out_len == -1 && errno != EAGAIN)
			socket.socket_error();

		/*
		 * Non-blocking write stalled
		 */
		if ((out_len == -1) && (errno == EAGAIN))
			handle_wakeup_remote_peers(socket);

		return out_len;

	} catch (Socket::Inaccessible) {
		return Errno(EINVAL);
	}
}


ssize_t Libc::socket_sendto(Socket &socket, void const *buf, ::size_t len, int flags,
                            sockaddr const *dest_addr, socklen_t dest_addrlen)
{
	return do_sendto(socket, buf, len, flags, dest_addr, dest_addrlen);
}


ssize_t Libc::socket_send(Socket &socket, void const *buf, ::size_t len, int flags)
{
	/* identical to sendto() with a NULL dest_addr argument */
	return socket_sendto(socket, buf, len, flags, nullptr, 0);
}


int Libc::socket_getsockopt(Socket &socket, int level, int optname,
                            void *optval, socklen_t *optlen)
{
	if (!optval) return Errno(EFAULT);
	if (*optlen < sizeof(int) || *optlen > sizeof(long)) return Errno(EINVAL);

	auto read_sockopt = [&]()
	{
		int err = socket.with_sockopt_fd(optname, [&](int fd) {
			ssize_t n =  ::read(fd, optval, *optlen);
			*(unsigned *)optlen = n;
			return n;
		});

		return err ? Errno(err) : 0;
	};

	switch (level) {
	case SOL_SOCKET:
		switch (optname) {

		/* emulated opts */
		case SO_ERROR:
			if (socket.state() == Socket::CONNECTING) {

				int connect_status = socket.read_connect_status();

				if (connect_status == 0) {
					*(int*)optval = 0;
					socket.state(Socket::CONNECTED);
				} else {
					*(int*)optval = errno;
					socket.state(Socket::CONNECT_ABORTED);
				}

				return 0;
			}

			/* not yet implemented - but return true */
			*(int *)optval = 0;
			return 0;
		case SO_TYPE:
			switch (socket.proto()) {
			case Socket::Proto::UDP: *(int *)optval = SOCK_DGRAM;  break;
			case Socket::Proto::TCP: *(int *)optval = SOCK_STREAM; break;
			}
			return 0;

		/* handled by VFS/IP-stack */
		case SO_REUSEADDR:
		case SO_KEEPALIVE:

			return read_sockopt();

		default: return Errno(ENOPROTOOPT);
		}
	case IPPROTO_TCP:
		switch (optname) {

		/* handled by VFS/IP-stack */
		case TCP_KEEPCNT:
		case TCP_KEEPIDLE:
		case TCP_KEEPINTVL:

			return read_sockopt();

		default: return Errno(ENOPROTOOPT);
		}

	default: return Errno(EINVAL);
	}
}


int Libc::socket_setsockopt(Socket &socket, int level, int optname,
                            void const *optval, socklen_t optlen)
{
	if (!optval) return Errno(EFAULT);

	if (optlen < sizeof(int) || optlen > sizeof(long)) return Errno(EINVAL);

	auto write_sockopt = [&]()
	{
		int err = socket.with_sockopt_fd(optname, [&](int fd) {
			return ::write(fd, optval, optlen); });

		return err ? Errno(err) : 0;
	};

	switch (level) {
	case SOL_SOCKET:
		switch (optname) {

		/* emulated */
		case SO_LINGER:
			{
				linger *l = (linger *)optval;
				if (l->l_onoff == 0)
					return 0;
			}

		/* handled by VFS/IP-stack */
		case SO_REUSEADDR:
		case SO_KEEPALIVE:

			return write_sockopt();

		default: return Errno(ENOPROTOOPT);
		}
	case IPPROTO_TCP:
		switch (optname) {

		/* emulated */
		case TCP_NODELAY:
			return 0;

		/* handled by VFS/IP-stack */
		case TCP_KEEPCNT:
		case TCP_KEEPIDLE:
		case TCP_KEEPINTVL:

			return write_sockopt();

		default: return Errno(ENOPROTOOPT);
		}

	default: return Errno(EINVAL);
	}
}


int Libc::socket_shutdown(Socket &socket, int how)
{
	/* TODO ENOTCONN */
	/* TODO EINVAL - returned if 'how' is not supported but we don't support
	   shutdown at all currently */

	return 0;
}


Libc::Create_socket_result Libc::create_socket(int domain, int type, int protocol)
{
	Socket::Absolute_path path(_config_ptr->socket.string());

	if (path == "") {
		error(__func__, ": socket fs not mounted");
		return Errno { EACCES };
	}
	/*
	 * The socket type (in the lower bits) maybe ORed with SOCK_CLOEXEC and
	 * SOCK_NONBLOCK options (in the higher bits). Currently, supported values
	 * are SOCK_STREAM (1) and SOCK_DGRAM (2), so just take the lower 2 bits.
	 */
	int const sock_type = type & 3;
	if ((sock_type != SOCK_STREAM || (protocol != 0 && protocol != IPPROTO_TCP))
	 && (sock_type != SOCK_DGRAM  || (protocol != 0 && protocol != IPPROTO_UDP))) {
		error(__func__,
		      ": socket with type=", (Hex)type,
		      " protocol=", (Hex)protocol, " not supported");
		return Errno { EAFNOSUPPORT };
	}

	/* socket is ensured to be TCP or UDP */
	using Proto = Socket::Proto;
	Proto proto = (sock_type == SOCK_STREAM) ? Proto::TCP : Proto::UDP;
	try {
		switch (proto) {
		case Proto::TCP: path.append("/tcp"); break;
		case Proto::UDP: path.append("/udp"); break;
		}

		path.append("/new_socket");
		int handle_fd = ::open(path.base(), O_RDONLY);
		if (handle_fd < 0) {
			error("failed to open new socket at ", path);
			return Errno { EACCES };
		}
		Socket &socket = *new (kernel_heap()) Socket(proto, handle_fd);

		int flags = 0;
		if (type & SOCK_NONBLOCK) flags |= O_NONBLOCK;
		if (type & SOCK_CLOEXEC)  flags |= O_CLOEXEC;
		socket.fd_flags(flags);
		return socket;

	} catch (Socket::New_socket_failed) { return Errno { ENFILE }; }
}


void Libc::destroy_socket(Socket &socket)
{
	Genode::destroy(kernel_heap(), &socket);
}


static int read_ifaddr_file(sockaddr_in &sockaddr, Socket::Absolute_path const &path)
{
	Socket::Host_string address;
	Socket::Port_string service;
	*service.base() = '0';

	{
		FILE *fp = ::fopen(path.base(), "r");
		if (!fp) return -1;

		::fscanf(fp, "%s\n", address.base());
		::fclose(fp);
	}

	try { sockaddr = sockaddr_in_struct(address, service); }
	catch (...) { return -1; }

	return 0;
}


extern "C" int getifaddrs(struct ifaddrs **ifap)
{
	static Pthread_mutex mutex;

	Pthread_mutex::Guard guard(mutex);

	static sockaddr_in address;
	static sockaddr_in netmask   { 0 };
	static sockaddr_in broadcast { 0 };
	static char        name[1] { };

	static ifaddrs ifaddr {
		.ifa_name      = name,
		.ifa_flags     = IFF_UP,
		.ifa_addr      = (sockaddr*)&address,
		.ifa_netmask   = (sockaddr*)&netmask,
		.ifa_broadaddr = (sockaddr*)&broadcast,
	};

	*ifap = &ifaddr;

	using Absolute_path = Socket::Absolute_path;

	Absolute_path const root(_config_ptr->socket.string());

	if (read_ifaddr_file(address, Absolute_path("address", root.base())))
		return -1;

	read_ifaddr_file(netmask, Absolute_path("netmask", root.base()));
	return 0;
}


extern "C" void freeifaddrs(struct ifaddrs *) { }


int Libc::socket_fcntl(Socket &socket, int cmd, long arg)
{
	switch (cmd) {
	case F_GETFD:
		return socket.fd_flags();
	case F_SETFD:
		socket.fd_flags(arg);
		return 0;
	case F_GETFL:
		return socket.fd_flags() | O_RDWR;
	case F_SETFL:
		socket.fd_flags(arg);
		return 0;
	default:
		error(__func__, " command ", cmd, " not supported on sockets");
		return Errno(EINVAL);
	}
}


ssize_t Libc::socket_read(Socket &socket, void *buf, ::size_t count)
{
	ssize_t const ret = do_recvfrom(socket, buf, count, 0, nullptr, nullptr);
	if (ret != -1) return ret;

	/* TODO map recvfrom errno to write errno */
	switch (errno) {
	default: return Errno(errno);
	}
}


ssize_t Libc::socket_write(Socket &socket, const void *buf, ::size_t count)
{
	ssize_t const ret = do_sendto(socket, buf, count, 0, nullptr, 0);
	if (ret != -1) return ret;

	/* TODO map sendto errno to write errno */
	switch (errno) {
	default: return Errno(errno);
	}
}


int Libc::socket_poll(Pollfd fds[], int nfds)

{
	int nready = 0;

	auto fn = [&]
	{
		for (int pollfd_index = 0; pollfd_index < nfds; pollfd_index++) {

			bool fd_ready = false;

			if (fds[pollfd_index].events & (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)) {

				try {
					Socket *socket_ptr = fds[pollfd_index].fdo->socket_ptr;
					if (socket_ptr && socket_ptr->read_ready()) {
						*fds[pollfd_index].revents |= POLLIN;
						fd_ready = true;
					}
				} catch (Socket::Inaccessible) { }
			}

			if (fds[pollfd_index].events & (POLLOUT | POLLWRNORM | POLLWRBAND)) {
				try {
					Socket *socket_ptr = fds[pollfd_index].fdo->socket_ptr;
					if (socket_ptr && socket_ptr->write_ready()) {
						*fds[pollfd_index].revents |= POLLOUT;
						fd_ready = true;
					}
				} catch (Socket::Inaccessible) { }
			}

			/* XXX POLLERR not supported */

			if (fd_ready)
				nready++;
		}
		return Fn::COMPLETE;
	};

	if (Libc::Kernel::kernel().main_context() && Libc::Kernel::kernel().main_suspended()) {
		fn();
	} else {
		monitor().monitor(fn);
	}

	return nready;
}


int Libc::socket_ioctl(Socket &socket, unsigned long request, char *buf)
{
	if (request == FIONREAD) {
		int *count = (int *)buf;

		/*
		 * XXX The socket-buffer fill level is currently unknown. Thus, we
		 * just check for read-ready and pretend 64 KiB are readable.
		 */
		if (socket.read_ready())
			*count = 64*1024;
		else
			*count = 0;

		return 0;
	}

	error(__func__, " request ", request, " not supported on sockets");
	return Errno(ENOTTY);
}


Libc::Socket_path Libc::socket_path(Socket const &socket)
{
	return { socket.path().string() };
}

