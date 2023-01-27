/*
 * \brief  Core implementation of the PD session interface
 * \author Alexander Boettcher
 * \date   2022-12-02
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <cpu/cpu_state.h>
#include <pd_session_component.h>

using namespace Genode;
using State = Genode::Pd_session::Managing_system_state;


State Pd_session_component::managing_system(State const &request)
{
	bool const suspend = (_managing_system == Managing_system::PERMITTED) &&
	                     (request.trapno   == State::ACPI_SUSPEND_REQUEST);
	State respond { };

	if (!suspend) {
		/* report failed attempt */
		respond.trapno = 0;
		return respond;
	}

	/*
	 * The trapno/ip/sp registers used below are just convention to transfer
	 * the intended sleep state S0 ... S5. The values are read out by an
	 * ACPI AML component and are of type TYP_SLPx as described in the
	 * ACPI specification, e.g. TYP_SLPa and TYP_SLPb. The values differ
	 * between different PC systems/boards.
	 *
	 * \note trapno/ip/sp registers are chosen because they exist in
	 *       Managing_system_state for x86_32 and x86_64.
	 */
	uint8_t const sleep_type_a = uint8_t(request.ip);
	uint8_t const sleep_type_b = uint8_t(request.sp);

	respond.trapno = Kernel::suspend(sleep_type_a, sleep_type_b);

	return respond;
}


/***************************
 ** Dummy implementations **
 ***************************/

bool Pd_session_component::assign_pci(addr_t, uint16_t) { return true; }


void Pd_session_component::map(addr_t, addr_t) { }

