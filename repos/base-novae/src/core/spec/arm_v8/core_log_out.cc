/*
 * \brief  Kernel-specific core's 'log' backend
 * \author Alexander Boettcher
 * \date   2025-03-15
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <core_log.h>

void Core::Core_log::out(char const)
{
	*(unsigned *)0 = 0xdead;
}
