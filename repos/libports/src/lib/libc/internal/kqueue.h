/*
 * \brief  kqueue interface
 * \author Norman Feske
 * \date   2026-06-16
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__KQUEUE_H_
#define _LIBC__INTERNAL__KQUEUE_H_

/* Libc includes */
#include <internal/types.h>

namespace Libc {

	class Kqueue;

	Kqueue &create_kqueue(Genode::Allocator &);

	void destroy_kqueue(Kqueue &);
}

#endif /* _LIBC__INTERNAL__KQUEUE_H_ */
