/*
 * \brief  Libc-internal file operations
 * \author Norman Feske
 * \date   2019-09-23
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__FILE_OPERATIONS_H_
#define _LIBC__INTERNAL__FILE_OPERATIONS_H_

/* Genode includes */
#include <os/path.h>

/* libc includes */
#include <limits.h>   /* for 'PATH_MAX' */

/* libc-internal includes */
#include <internal/types.h>

namespace Libc {
	using Absolute_path = Genode::Path<PATH_MAX>;

	template <typename RET>
	static RET with_joined(char const *p1, char const *p2, auto const &fn)
	{
		try {
			Absolute_path joined { p1 };
			joined.append_element(p2);
			return fn(joined.string());
		}
		catch (...) { return Errno(ENAMETOOLONG); }
	}
}

#endif /* _LIBC__INTERNAL__FILE_OPERATIONS_H_ */
