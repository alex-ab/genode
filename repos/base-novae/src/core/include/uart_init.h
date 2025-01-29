/*
 * \brief  Uart init definition
 * \author Alexander Boettcher
 * \date   2025-04-08
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* core includes */
#include <types.h>

namespace Core {

	void setup_uart(addr_t);
}
