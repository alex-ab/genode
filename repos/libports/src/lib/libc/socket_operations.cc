/*
 * \brief  libc socket operations
 * \author Christian Helmuth
 * \author Christian Prochaska
 * \author Norman Feske
 * \date   2015-06-23
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/env.h>
#include <os/path.h>
#include <util/token.h>

/* libc includes */
extern "C" {
#include <sys/wait.h>
#include <libc_private.h>
#include <fcntl.h>
}
#include <libc/allocator.h>

/* libc-internal includes */
#include <internal/file.h>
#include <internal/socket.h>
#include <internal/errno.h>
#include <internal/init.h>


using namespace Libc;

static Config      const *_config_ptr;
static Genode::Allocator *_kernel_heap;

static Config const &config()
{
	struct Missing_call_of_init_socket_operations : Genode::Exception { };
	if (!_config_ptr)
		throw Missing_call_of_init_socket_operations();
	return *_config_ptr;
}

static Genode::Allocator &kernel_heap()
{
	struct Missing_call_of_init_socket_operations : Genode::Exception { };
	if (!_kernel_heap)
		throw Missing_call_of_init_socket_operations();
	return *_kernel_heap;
}


void Libc::init_socket_operations(Genode::Allocator &kernel_heap,
                                  Fds &fds, Config const &config)
{
	_kernel_heap = &kernel_heap;
	_fds_ptr     = &fds;
	_config_ptr  = &config;
}


#define __SYS_(ret_type, name, args, body) \
	extern "C" {\
	ret_type  __sys_##name args body \
	ret_type       _##name args __attribute__((alias("__sys_" #name))); \
	ret_type          name args __attribute__((alias("__sys_" #name))); \
	} \


template <typename FN>
static auto with_socket(int libc_fd, FN const &fn)
-> typename Trait::Functor<decltype(&FN::operator())>::Return_type
{
	using Ret = typename Trait::Functor<decltype(&FN::operator())>::Return_type;

	return with_fd(libc_fd, "socketfn", [&] (File_descriptor &fd) -> Ret {
		if (!fd.socket_ptr)
			return Errno(ENOTSOCK);

		return fn(*fd.socket_ptr);
	});
}


/***********************
 ** Address functions **
 ***********************/

extern "C" int getpeername(int libc_fd, sockaddr *addr, socklen_t *addrlen)
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_getpeername(socket, addr, addrlen); });
}


extern "C" __attribute__((alias("getpeername")))
int _getpeername(int libc_fd, sockaddr *addr, socklen_t *addrlen);


extern "C" int getsockname(int libc_fd, sockaddr *addr, socklen_t *addrlen)
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_getsockname(socket, addr, addrlen); });
}


extern "C" __attribute__((alias("getsockname")))
int _getsockname(int libc_fd, sockaddr *addr, socklen_t *addrlen);


/**************************
 ** Socket transport API **
 **************************/

__SYS_(int, accept, (int libc_fd, sockaddr *addr, socklen_t *addrlen),
{
	return with_socket(libc_fd, [&] (Socket &socket) {

		auto const allocated_libc_id =
			fds().with_alloc([&] (Fds::Bits &bits, Fds::Space &) {
				return bits.alloc(); });

		return allocated_libc_id.convert<int>(
			[&] (addr_t const libc_id) {
				return socket_accept(socket, addr, addrlen).convert<int>(
					[&] (Socket &accept_socket) {
						return fds().with_space([&] (Fds::Space &space) {
							new (kernel_heap())
								File_descriptor(space, int(libc_id), accept_socket,
							                    socket_path(accept_socket));
							return int(libc_id);
						});
					},
					[&] (Errno e) {
						fds().with_alloc([&] (Fds::Bits &bits, Fds::Space &) {
							bits.free(libc_id); });
						return e;
					});
			},
			[&] (Fds::Bits::Error) -> int { return Errno { EMFILE }; });
	});
})


__SYS_(int, accept4, (int libc_fd, struct sockaddr *addr, socklen_t *addrlen, int /*flags*/),
{
	return accept(libc_fd, addr, addrlen);
})


extern "C" int bind(int libc_fd, sockaddr const *addr, socklen_t addrlen)
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_bind(socket, addr, addrlen); });
}


extern "C" __attribute__((alias("bind")))
int _bind(int libc_fd, sockaddr const *addr, socklen_t addrlen);


static int block_until_write_ready(int libc_fd, unsigned timeout_seconds)
{
	fd_set writefds;
	FD_ZERO(&writefds);
	FD_SET(libc_fd, &writefds);

	struct timeval timeout { int(timeout_seconds), 0 };
	return select(libc_fd + 1, NULL, &writefds, NULL, &timeout);
}


__SYS_(int, connect, (int libc_fd, sockaddr const *addr, socklen_t addrlen),
{
	int ret = with_socket(libc_fd, [&] (Socket &socket) {
		return socket_try_connect(socket, addr, addrlen); });

	auto need_block = [&]
	{
		int const flags = fcntl(libc_fd, F_GETFL, 0);
		return !(flags & O_NONBLOCK);
	};

	if (!need_block())
		return ret;

	auto connecting = [&] { return ret < 0 && errno == EINPROGRESS; };

	while (connecting()) {

		int const select_ret =
			block_until_write_ready(libc_fd, config().conn_timeout.seconds);

		if (select_ret < 0)
			break; /* errno has been set by select() */

		ret = with_socket(libc_fd, [&] (Socket &socket) {

			if (select_ret == 0)
				return socket_connect_timed_out(socket);

			/* apply 'connect_status' change */
			return socket_try_connect(socket, addr, addrlen);
		});
	}
	return ret;
})


extern "C" int listen(int libc_fd, int backlog)
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_listen(socket, backlog); });
}


__SYS_(ssize_t, recvfrom, (int libc_fd, void *buf, ::size_t len, int flags,
                           sockaddr *src_addr, socklen_t *src_addrlen),
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_recvfrom(socket, buf, len, flags, src_addr, src_addrlen); });
})


__SYS_(ssize_t, recv, (int libc_fd, void *buf, ::size_t len, int flags),
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_recv(socket, buf, len, flags); });
})


__SYS_(ssize_t, recvmsg, (int libc_fd, msghdr *msg, int flags),
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_recvmsg(socket, msg, flags); });
})


__SYS_(ssize_t, sendto, (int libc_fd, void const *buf, ::size_t len, int flags,
                          sockaddr const *dest_addr, socklen_t dest_addrlen),
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_sendto(socket, buf, len, flags, dest_addr, dest_addrlen); });
})


extern "C" ssize_t send(int libc_fd, void const *buf, ::size_t len, int flags)
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_send(socket, buf, len, flags); });
}


extern "C" int getsockopt(int libc_fd, int level, int optname,
                          void *optval, socklen_t *optlen)
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_getsockopt(socket, level, optname, optval, optlen); });
}


extern "C" __attribute__((alias("getsockopt")))
int _getsockopt(int libc_fd, int level, int optname,
                void *optval, socklen_t *optlen);


extern "C" int setsockopt(int libc_fd, int level, int optname,
                          void const *optval, socklen_t optlen)
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_setsockopt(socket, level, optname, optval, optlen); });
}


extern "C" __attribute__((alias("setsockopt")))
int _setsockopt(int libc_fd, int level, int optname,
                void const *optval, socklen_t optlen);


extern "C" int shutdown(int libc_fd, int how)
{
	return with_socket(libc_fd, [&] (Socket &socket) {
		return socket_shutdown(socket, how); });
}


__SYS_(int, socket, (int domain, int type, int protocol),
{
	if (_config_ptr->socket.length() <= 1)
		return Errno { ENOTSOCK };

	auto allocated_libc_id = fds().with_alloc(
		[&] (Fds::Bits &bits, Fds::Space &) { return bits.alloc(); });

	return allocated_libc_id.convert<int>( [&] (addr_t const libc_id) {
		return create_socket(domain, type, protocol).convert<int>(
			[&] (Socket &new_socket) {
				return fds().with_space([&] (Fds::Space &space) {
					new (kernel_heap())
						File_descriptor(space, int(libc_id), new_socket,
					                    socket_path(new_socket));
					return int(libc_id);
				});
			},
			[&] (Errno e) {
				fds().with_alloc([&] (Fds::Bits &bits, Fds::Space &) {
				bits.free(libc_id); });
				return e;
			});
		},
		[&] (Fds::Bits::Error) -> int { return Errno { EMFILE }; });
})
