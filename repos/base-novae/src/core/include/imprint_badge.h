/*
 * \brief  Utility to imprint a badge into a NOVAe portal
 * \author Norman Feske
 * \date   2016-03-03
 */

/*
 * Copyright (C) 2016-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* NOVAe includes */
#include <novae/syscalls.h>
#include <novae_util.h>

static inline bool imprint_badge(auto core_cap_sel, Genode::addr_t pt_sel,
                                 Genode::addr_t badge, Genode::addr_t mtd = 0)
{
	using namespace Novae;
	using namespace Genode;

	/* assign badge to portal */
	auto ret = pt_ctrl(pt_sel, badge, mtd);
	if (ret != Novae::NOVA_OK)
		error(__func__, ":", __LINE__, " returned ", ret);

	/* remove RIGHT_PT_CTRL to prevent subsequent imprint attempts */
	ret = modify(core_cap_sel, Obj_crd(pt_sel, 0,
	                                   Obj_crd::RIGHT_PT_EVENT |
	                                   Obj_crd::RIGHT_PT_CALL));
	if (ret != Novae::NOVA_OK)
		error(__func__, ":", __LINE__, " returned ", ret);

	return true;
}
