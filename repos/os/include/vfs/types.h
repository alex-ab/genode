/*
 * \brief  Types used by VFS
 * \author Norman Feske
 * \date   2014-04-07
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__TYPES_H_
#define _INCLUDE__VFS__TYPES_H_

#include <util/list.h>
#include <util/misc_math.h>
#include <util/string.h>
#include <base/node.h>
#include <base/env.h>
#include <base/signal.h>
#include <base/allocator.h>
#include <dataspace/client.h>
#include <os/path.h>

namespace Genode::Vfs {

	enum { MAX_PATH_LEN = 512 };

	using file_offset = long long;
	using file_size = unsigned long long;

	struct Timestamp { uint64_t ms_since_1970; };

	enum class Node_type {
		DIRECTORY,
		SYMLINK,
		CONTINUOUS_FILE,
		TRANSACTIONAL_FILE
	};

	struct Node_rwx
	{
		bool readable;
		bool writeable;
		bool executable;

		static Node_rwx ro()  { return { .readable   = true,
		                                 .writeable  = false,
		                                 .executable = false }; }

		static Node_rwx wo()  { return { .readable   = false,
		                                 .writeable  = true,
		                                 .executable = false }; }

		static Node_rwx rw()  { return { .readable   = true,
		                                 .writeable  = true,
		                                 .executable = false }; }

		static Node_rwx rx()  { return { .readable   = true,
		                                 .writeable  = false,
		                                 .executable = true }; }

		static Node_rwx rwx() { return { .readable   = true,
		                                 .writeable  = true,
		                                 .executable = true }; }
	};

	using Absolute_path = Path<MAX_PATH_LEN>;

	struct Scanner_policy_path_element
	{
		static bool identifier_char(char c, unsigned /* i */)
		{
			return (c != '/') && (c != 0);
		}

		static bool end_of_quote(const char *s)
		{
			return s[0] != '\\' && s[1] == '\"';
		}
	};

	struct Parent_fs : Noncopyable, Interface
	{
		virtual void notify_watchers(Span const &) = 0;
	};

	static inline void with_compound_dir(Span const &path, auto const &fn)
	{
		char const *s = path.start; size_t n = path.num_bytes;

		/* if path is directory, drop trailing slash, ignore multiple slashes */
		while (n > 0 && s[n - 1] == '/') n--;

		/* search from end to front for the slash of the compound directory */
		while (n > 0 && s[n - 1] != '/') n--;

		if (n) fn(Span(s, n));
	}

	using Watch_result = Attempt<Ok, Alloc_error>;

	struct File_system_factory;
}

#endif /* _INCLUDE__VFS__TYPES_H_ */
