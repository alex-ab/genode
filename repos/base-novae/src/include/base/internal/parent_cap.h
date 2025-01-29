/*
 * \brief  Interface to obtain the parent capability for the component
 * \author Norman Feske
 * \author Alexander Boettcher
 * \date   2010-01-26
 *
 * The parent portal is, by convention, capability selector 'PT_CAP_PARENT'
 * supplied with the initial portals when the PD is created.
 */

/*
 * Copyright (C) 2010-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* Genode includes */
#include <parent/capability.h>

/* NOVAe includes */
#include <novae/capability_space.h>


namespace Genode {

	static inline Parent_capability parent_cap()
	{
		return reinterpret_cap_cast<Parent>(
			Capability_space::import(Novae::PT_SEL_PARENT));
	}
}
