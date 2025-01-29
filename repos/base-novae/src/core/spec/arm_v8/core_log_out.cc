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
#include <platform.h>
#include <novae_util.h>

#include <uart_init.h>

#include <drivers/uart/pl011.h>


using Serial = Genode::Pl011_uart;


enum Board {
	UART_BASE  = 0x09000000,
	UART_SIZE  = 0x1000,
	UART_CLOCK = 250000000,

	UART_BAUD_RATE = 115200,
};


Serial & uart(Genode::addr_t const mapped_mmio = 0)
{
	static Serial serial { mapped_mmio,
	                       Board::UART_CLOCK,
	                       Board::UART_BAUD_RATE };
	return serial;
}


void Core::setup_uart(Genode::addr_t map_to_virt)
{
	async_map(Core::Platform::kernel_host_sel(),
	          Core::Platform::core_host_sel(),
	          Novae::Mem_crd(UART_BASE   >> 12, 0),
	          Novae::Mem_crd(map_to_virt >> 12, 0, Novae::Rights::rw()));

	uart(map_to_virt);
}


void Core::Core_log::out(char const c)
{
	if (c == '\n')
		uart().put_char('\r');

	uart().put_char(c);
}
