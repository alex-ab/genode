/*
 * \brief  Linux emulation environment specific to this driver - Intel opregion
 * \author Alexander Boettcher
 * \date   2022-01-21
 */

/*
 * Copyright (C) 2022-2025 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */


#include <base/log.h>
#include <lx_kit/env.h>

#include <lx_emul.h>
#include <lx_emul/io_mem.h>

extern "C" void * intel_io_mem_map(unsigned long const phys,
                                   unsigned long const size)
{
	using namespace Genode;

	static unsigned long opregion_phys = 0;
	static unsigned long opregion_size = 0;

	if (phys < OPREGION_PSEUDO_PHYS_ADDR || !size) {
		warning("Unknown ", __func__, " range ", Hex_range(phys, size));
		return nullptr;
	}

	if (!opregion_size)
		Lx_kit::env().devices.for_each([&] (auto &d) {
			if (d.name() != "intel_opregion")
				return;

			d.for_each_io_mem([&] (auto const &mem) {
				opregion_phys = mem.addr;
				opregion_size = mem.size;
			});
		});

	/*
	 * we have to substract the pseudo physical address
	 * we returned when reading the ASLS from config space
	 */
	addr_t const offset = phys - OPREGION_PSEUDO_PHYS_ADDR;
	if ((offset + size) > opregion_size) {
		warning("Unknown ", __func__, " range ", Hex_range(phys, size));
		return nullptr;
	}

	return lx_emul_io_mem_map(opregion_phys + offset, opregion_size - offset);
}
