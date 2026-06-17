/*
 * \brief  Libc-internal socket interface
 * \author Norman Feske
 * \date   2026-06-23
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__SOCKET_H_
#define _LIBC__INTERNAL__SOCKET_H_

/* libc includes */
#include <sys/types.h>
#include <sys/socket.h>

/* libc-internal includes */
#include <internal/poll.h>
#include <internal/errno.h>

namespace Libc {

	class Socket;

	using Create_socket_result = Unique_attempt<Socket &, Errno>;
	Create_socket_result create_socket(int domain, int type, int protocol);

	void destroy_socket(Socket &);

	using Accept_result = Unique_attempt<Socket &, Errno>;
	Accept_result socket_accept(Socket &, sockaddr *, socklen_t *);

	int     socket_getpeername(Socket &, sockaddr *, socklen_t *);
	int     socket_getsockname(Socket &, sockaddr *, socklen_t *);
	int     socket_bind(Socket &, sockaddr const *, socklen_t);
	int     socket_try_connect(Socket &, sockaddr const *, socklen_t);
	int     socket_connect_timed_out(Socket &);
	int     socket_listen(Socket &, int);
	ssize_t socket_recvfrom(Socket &, void *, ::size_t, int, sockaddr *, socklen_t *);
	ssize_t socket_recv(Socket &, void *, ::size_t, int);
	ssize_t socket_recvmsg(Socket &, msghdr *, int);
	ssize_t socket_sendto(Socket &, void const *, ::size_t, int, sockaddr const *, socklen_t);
	ssize_t socket_send(Socket &, void const *, ::size_t, int);
	int     socket_getsockopt(Socket &, int, int, void *, socklen_t *);
	int     socket_setsockopt(Socket &, int, int, void const *, socklen_t);
	int     socket_shutdown(Socket &, int);
	int     socket_fcntl(Socket &, int, long);
	ssize_t socket_read(Socket &, void *, ::size_t);
	ssize_t socket_write(Socket &, const void *, ::size_t);
	int     socket_poll(Pollfd fds[], int);
	int     socket_ioctl(Socket &, unsigned long, char *);

	Socket_path socket_path(Socket const &);
}

#endif /* _LIBC__INTERNAL__SOCKET_H_ */
