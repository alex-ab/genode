/*
 * \brief  Helper code used by core as base framework
 * \author Alexander Boettcher
 * \date   2012-08-08
 */

/*
 * Copyright (C) 2012-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <base/log.h>
#include <base/thread.h>

__attribute__((always_inline))
inline void nova_die()
{
	asm volatile ("ud2a");
}


inline void request_event_portal(Genode::addr_t const cap,
                                 Genode::addr_t const sel, Genode::addr_t event)
{
	Genode::Thread * myself = Genode::Thread::myself();
	Novae::Utcb *utcb = reinterpret_cast<Novae::Utcb *>(myself->utcb());

	/* request event-handler portal */
	utcb->msg()[0] = event;
	utcb->msg()[1] = sel;

	unsigned mtd = 1;
	auto     res = Novae::call(cap, mtd);

	if (res)
		Genode::error("request of event (", Genode::Hex(event), ") ",
		              "capability selector failed (res=", res, ")");
}


inline void request_signal_sm_cap(Genode::addr_t const cap,
                                  Genode::addr_t const sel)
{
	request_event_portal(cap, sel, ~0UL - 1);
}
