/*
 * \brief  Integer type definitions used by NOVA syscall bindings
 * \author Norman Feske
 * \date   2010-01-15
 */

/*
 * Copyright (C) 2010-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <base/fixed_stdint.h>

namespace Novae {

	typedef unsigned long    mword_t;
	typedef unsigned char    uint8_t;
	typedef Genode::uint16_t uint16_t;
	typedef Genode::uint32_t uint32_t;
	typedef Genode::uint64_t uint64_t;
}
