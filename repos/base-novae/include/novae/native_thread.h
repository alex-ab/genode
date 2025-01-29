/*
 * \brief  Kernel-specific thread meta data
 * \author Norman Feske
 * \date   2016-03-11
 *
 * On most platforms, the 'Genode::Native_thread' type is private to the
 * base framework. However, on NOVA, we make the type publicly available to
 * expose the low-level thread-specific capability selectors to user-level
 * virtual-machine monitors (Seoul or VirtualBox).
 */

/*
 * Copyright (C) 2016-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <base/stdint.h>
#include <base/native_capability.h>
#include <util/noncopyable.h>

namespace Genode { struct Native_thread; }


struct Genode::Native_thread : Noncopyable
{
	static constexpr unsigned long INVALID_INDEX = ~0UL;

	addr_t ec_sel     = INVALID_INDEX;   /* selector for execution context */
	addr_t exc_pt_sel = INVALID_INDEX;   /* base of event portal window */
	addr_t initial_ip = 0;

	Native_capability pager_cap { };

	Native_thread() { }

	/* ec_sel is invalid until thread gets started */
	bool ec_valid() const { return ec_sel != INVALID_INDEX; }
};
