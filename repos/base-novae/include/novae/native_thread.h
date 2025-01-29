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
 * Copyright (C) 2016-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <base/stdint.h>
#include <base/native_capability.h>

namespace Genode { struct Native_thread; }

struct Genode::Native_thread
{
	static constexpr unsigned long INVALID_INDEX = ~0UL;

	addr_t ec_sel     { 0 };      /* selector for execution context */
	addr_t exc_pt_sel { 0 };      /* base of event portal window */
	addr_t initial_ip { 0 };      /* initial IP of local thread */

	Native_capability pager_cap { };

	Native_thread() : ec_sel(INVALID_INDEX),
	                  exc_pt_sel(INVALID_INDEX),
	                  initial_ip(0) { }
};
