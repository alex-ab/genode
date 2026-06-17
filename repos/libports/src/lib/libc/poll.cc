/*
 * \brief  poll() implementation
 * \author Josef Soentgen
 * \author Christian Helmuth
 * \author Emery Hemingway
 * \date   2012-07-12
 */

/*
 * Copyright (C) 2010-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Libc includes */
#include <sys/poll.h>

/* internal includes */
#include <internal/errno.h>
#include <internal/fds.h>
#include <internal/file.h>
#include <internal/init.h>
#include <internal/monitor.h>
#include <internal/signal.h>
#include <internal/fs.h>

using namespace Libc;

static Monitor      *_monitor_ptr;
static Libc::Signal *_signal_ptr;

void Libc::init_poll(Signal &signal, Monitor &monitor, Fds &fds)
{
	_signal_ptr  = &signal;
	_monitor_ptr = &monitor;
	_fds_ptr     = &fds;
}


extern "C" int
__attribute__((weak))
poll(struct pollfd pollfds[], nfds_t nfds, int timeout_ms)
{
	struct Missing_call_of_init_poll : Genode::Exception { };
	if (!_fds_ptr || !_monitor_ptr || !_signal_ptr)
		throw Missing_call_of_init_poll();

	/*
	 * Look up the file descriptor objects early-on to reduce repeated
	 * overhead.
	 *
	 * According to POSIX poll(2), a poolfd entry is ignored if 'fd' is
	 * negative, but 'revents' must be cleared (set to zero) nevertheless.
	 * Note, negative file descriptors result in a nullptr in fdo on calls to
	 * 'find_by_libc_fd()', which is checked against above and will result in
	 * the file descriptor being skipped.
	 */

	Pollfd internal_pollfds[nfds];

	for (nfds_t pollfd_index = 0; pollfd_index < nfds; pollfd_index++) {
		pollfds[pollfd_index].revents = 0;
		internal_pollfds[pollfd_index].fdo = fds().with_space([&] (Fds::Space &space) {
			return space.apply<File_descriptor>({ addr_t(pollfds[pollfd_index].fd) },
				[&] (File_descriptor &fd) { return &fd; },
				[&]                       { return nullptr; }); });
		internal_pollfds[pollfd_index].events = pollfds[pollfd_index].events;
		internal_pollfds[pollfd_index].revents = &pollfds[pollfd_index].revents;
	}

	auto poll_sockets_and_files = [&]
	{
		return socket_poll(internal_pollfds, nfds)
		     + Fs::poll(*_monitor_ptr, internal_pollfds, nfds);
	};

	int nready = 0;
	_monitor_ptr->monitor([&] {
		nready = poll_sockets_and_files();
		return Monitor::Function_result::COMPLETE;
	});

	/* return if any descriptor is ready or an error occurred */
	if (nready != 0)
		return nready;

	/* return on zero-timeout */
	if (timeout_ms == 0)
		return 0;

	/* convert infinite timeout to monitor interface */
	if (timeout_ms < 0)
		timeout_ms = 0;

	unsigned const orig_signal_count = _signal_ptr->count();

	auto signal_occurred_during_poll = [&] ()
	{
		return (_signal_ptr->count() != orig_signal_count);
	};

	auto monitor_fn = [&] ()
	{
		nready = poll_sockets_and_files();

		if (nready != 0)
			return Monitor::Function_result::COMPLETE;

		if (signal_occurred_during_poll())
			return Monitor::Function_result::COMPLETE;

		return Monitor::Function_result::INCOMPLETE;
	};

	Monitor::Result const monitor_result =
		_monitor_ptr->monitor(monitor_fn, timeout_ms);

	if (monitor_result == Monitor::Result::TIMEOUT)
		return 0;

	if (signal_occurred_during_poll())
		return Errno(EINTR);

	return nready;
}


extern "C" __attribute__((weak, alias("poll")))
int __sys_poll(struct pollfd fds[], nfds_t nfds, int timeout_ms);


extern "C" __attribute__((weak, alias("poll")))
int _poll(struct pollfd fds[], nfds_t nfds, int timeout_ms);


extern "C" __attribute__((weak))
int ppoll(struct pollfd fds[], nfds_t nfds,
          const struct timespec *timeout,
          const sigset_t*)
{
	int timeout_ms = timeout ?
	                 (timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000) :
	                 -1;
	return poll(fds, nfds, timeout_ms);
}


extern "C" __attribute__((weak, alias("ppoll")))
int __sys_ppoll(struct pollfd fds[], nfds_t nfds,
                const struct timespec *timeout,
                const sigset_t*);
