/*
 * \brief  Capability helper
 * \author Norman Feske
 * \date   2016-06-27
 */

/*
 * Copyright (C) 2016-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* Genode includes */
#include <base/capability.h>

/* NOVAe includes */
#include <novae/syscalls.h>

namespace Genode { namespace Capability_space {

	static constexpr unsigned long INVALID_INDEX = ~0UL;

	using Ipc_cap_data = Novae::Crd;

	static inline Novae::Crd crd(Native_capability const &cap)
	{
		/*
		 * We store the 'Novae::Crd' value in place of the 'Data' pointer.
		 */
		addr_t value = (addr_t)cap.data();
		Novae::Crd crd = *(Novae::Crd *)&value;
		return crd;
	}

	static inline Native_capability import(addr_t sel, unsigned rights = 0x1f)
	{
		Novae::Obj_crd const crd = (sel == INVALID_INDEX)
		                        ? Novae::Obj_crd() : Novae::Obj_crd(sel, 0, rights);
		return Native_capability((Native_capability::Data *)crd.value());
	}
} }
