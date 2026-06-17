/*
 * \brief  Libc-internal poll interface
 * \author Norman Feske
 * \date   2026-06-16
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__POLL_H_
#define _LIBC__INTERNAL__POLL_H_

/* Libc includes */
#include <internal/types.h>
#include <sys/poll.h>   /* for 'struct pollfd' */

namespace Libc {

	struct File_descriptor;

	struct Pollfd
	{
		File_descriptor *fdo;
		short            events;
		/*
		 * This pointer points to 'revents' of the original
		 * 'struct pollfd' array.
		 */
		short           *revents;
	};
}

#endif /* _LIBC__INTERNAL__POLL_H_ */
