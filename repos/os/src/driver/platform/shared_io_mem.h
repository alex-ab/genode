/*
 * \brief  Platform driver - shared memory across devices
 * \author Alexander Boettcher
 * \date   2025-09-18
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__DRIVERS__PLATFORM__SHARED_IO_MEM_H_
#define _SRC__DRIVERS__PLATFORM__SHARED_IO_MEM_H_

#include <base/registry.h>
#include <io_mem_session/connection.h>

namespace Driver {
	using namespace Genode;

	class Shared_io_memory;
}


class Driver::Shared_io_memory : private Registry<Shared_io_memory>::Element
{
	private:

		Env &_env;

		using Range = Platform::Device_interface::Range;

		Range range;

		Io_mem_connection _io_mem;

	public:

		Shared_io_memory(Registry<Shared_io_memory> &registry, Env &env,
		                 Range r, bool prefetchable)
		:
			Registry<Shared_io_memory>::Element(registry, *this),
			_env(env), range(r),
			_io_mem(_env, r.start, r.size, prefetchable)
		{ }

		bool matches(Range const &r) const
		{
			return range.start == r.start && range.size == r.size;
		}

		Io_mem_session_capability io_mem_cap() const { return _io_mem.cap(); }
};

#endif /* _SRC__DRIVERS__PLATFORM__SHARED_IO_MEM_H_ */
