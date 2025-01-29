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

#include <bios_data_area.h>
#include <drivers/uart/x86_pc.h>


using Serial = Genode::X86_uart;


Serial & uart()
{
	enum { CLOCK = 0, BAUDRATE = 115200 };

	static Serial uart(Genode::Bios_data_area::singleton()->serial_port(),
	                   CLOCK, BAUDRATE);
	return uart;
}


void Core::setup_uart(Genode::addr_t const map_to_virt)
{
	/* map BDA region, console reads IO ports at UART_VIRT_ADDR + 0x400 */
	enum { BDA_PHYS = 0 };

	async_map(Core::Platform::kernel_host_sel(),
	          Core::Platform::core_host_sel(),
	          Novae::Mem_crd(BDA_PHYS    >> 12, 0),
	          Novae::Mem_crd(map_to_virt >> 12, 0, Novae::Rights::read_only()));

	uart();
}


void Core::Core_log::out(char const c)
{
	if (c == '\n')
		uart().put_char('\r');
	uart().put_char(c);
}
