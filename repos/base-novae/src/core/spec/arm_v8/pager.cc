/*
 * \brief  Copy thread state - ARM v8
 * \author Alexander Boettcher
 * \date   2012-08-23
 */

/*
 * Copyright (C) 2012-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <pager.h>

/* NOVAe includes */
#include <novae/syscalls.h>

using namespace Core;

void Pager_object::_copy_state_from_utcb(Novae::Utcb const &utcb)
{
	(void)utcb;
	error(__func__, " not implemented");
#if 0
	_state.thread.state = utcb.qual_1() ? Thread_state::State::EXCEPTION
	                                    : Thread_state::State::VALID;
#endif
}


void Pager_object::_copy_state_to_utcb(Novae::Utcb &utcb, unsigned &mtd) const
{
	(void)utcb;
	(void)mtd;
	error(__func__, " not implemented");

#if 0
	mtd = Novae::Mtd::GPR_0_7 | Novae::Mtd::GPR_8_15 |
	      Novae::Mtd::EIP     | Novae::Mtd::EFL;
#endif
}
