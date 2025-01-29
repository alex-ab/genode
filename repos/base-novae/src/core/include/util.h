/*
 * \brief  Core-internal utilities
 * \author Norman Feske
 * \date   2009-10-02
 */

/*
 * Copyright (C) 2009-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* Genode includes */
#include <rm_session/rm_session.h>

/* base-internal includes */
#include <base/internal/page_size.h>
#include <platform_thread.h>

namespace Core {

	constexpr size_t get_super_page_size_log2() { return 22; }
	constexpr size_t get_super_page_size()      { return 1 << get_super_page_size_log2(); }

	template <typename T>
	inline T trunc_page(T addr) { return addr & _align_mask(size_t(get_page_size_log2())); }

	template <typename T>
	inline T round_page(T addr) { return trunc_page(addr + get_page_size() - 1); }

	inline addr_t map_src_addr(addr_t /* core_local */, addr_t phys) { return phys; }

	inline Log2 kernel_constrained_map_size(Log2 size)
	{
		/* Novae::Mem_crd order has 5 bits available and is in 4K page units */
		return { min(size.log2, uint8_t((1 << 5) - 1 + 12)) };
	}

	inline unsigned scale_priority(auto const prio, auto const thread,
	                               auto const pd)
	{
		using Novae::Qpd;
		unsigned priority = Cpu_session::scale_priority(Qpd::DEFAULT_PRIORITY,
		                                                prio);
		if (priority == 0) {
			warning("priority of thread '", thread, "' below minimum - boost to 1");
			priority = 1;
		}
		if (priority > Novae::Qpd::DEFAULT_PRIORITY) {
			warning("priority of thread '", thread, "' above maximum - limit to ",
			        (unsigned)Qpd::DEFAULT_PRIORITY);
			priority = Qpd::DEFAULT_PRIORITY;
		}

		/* XXX
		 * - support for priority inheritance for Genode Spinlock is missing atm
		 * - w/o this feature one gets busy hanging of components, e.g timer
		 */

		if (priority != Qpd::DEFAULT_PRIORITY) {
			warning("priority ", priority, "->", unsigned(Qpd::DEFAULT_PRIORITY),
			        " changed, '", thread, "' '", pd, "'");

			priority = Qpd::DEFAULT_PRIORITY;
		}

		return priority;
	}
}
