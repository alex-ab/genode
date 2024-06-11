/*
 * \brief  PCI specific backend for ACPICA library
 * \author Alexander Boettcher
 * \date   2016-11-14
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/log.h>

#include "env.h"

extern "C" {
#include "acpi.h"
#include "acpiosxf.h"
}


/**
 * Utility for the formatted output of a (bus, device, function) triple
 */
struct Bdf
{
	unsigned char const bus, dev, fn;

	Bdf(unsigned char bus, unsigned char dev, unsigned char fn)
	: bus(bus), dev(dev), fn(fn) { }

	void print(Genode::Output &out) const
	{
		using Genode::Hex;
		Genode::print(out, Hex(bus, Hex::OMIT_PREFIX, Hex::PAD), ":",
		                   Hex(dev, Hex::OMIT_PREFIX, Hex::PAD), ".",
		                   Hex(fn,  Hex::OMIT_PREFIX), " ");
	}
};


template <typename S>
void for_each_element(auto const & head, S *, auto const &fn, auto const &fn_size)
{
	for(S const * e = reinterpret_cast<S const * const>(&head + 1);
	    e < reinterpret_cast<S const *>(reinterpret_cast<char const *>(&head) + head.Header.Length);
	    e = reinterpret_cast<S const *>(reinterpret_cast<char const *>(e) + fn_size(e)))
	{
		fn(*e);
	}
}


static void with_config(Bdf             const & bdf,
                        ACPI_TABLE_MCFG const & mcfg,
                        auto            const & fn)
{
	using namespace Genode;

	typedef ACPI_MCFG_ALLOCATION Mcfg_sub;

	for_each_element(mcfg, (Mcfg_sub *) nullptr, [&] (auto const & e) {

		/* bus_count * up to 32 devices * 8 function per device * 4k */
		uint32_t const bus_count  = e.EndBusNumber - e.StartBusNumber + 1;
		uint32_t const func_count = bus_count * 32 * 8;
		uint32_t const bus_start  = e.StartBusNumber * 32 * 8;

		error("bdf start=", bus_start, " func_count=", func_count,
			   " ", String<24>(Hex(e.Address)));

		/* force freeing I/O mem so that platform driver can use it XXX */
//		AcpiGenodeFreeIOMem(e.Address, 0x1000UL * func_count);

		if (bdf.bus < bus_start || bdf.bus >= bus_start + bus_count)
			return;

		unsigned bus_offset  = bdf.bus - bus_start;
		if (unsigned(bdf.dev * 8 + bdf.fn) >= func_count)
			return;

		auto func_offset = 0x1000 * ((bus_offset * 32 * 8) +
			                         (bdf.dev * 8 + bdf.fn));

		error("func_offset=", Genode::Hex(func_offset), " ", bdf);

		void * ptr = AcpiOsMapMemory(e.Address + func_offset, 0x1000);

		if (ptr)
			fn(ptr);

		if (ptr)
			AcpiOsUnmapMemory(ptr, 0x1000);

	}, [](Mcfg_sub const * const e) { return sizeof(*e); });
}


static bool read_config(Bdf const &bdf, UINT32 reg, UINT64 *value, UINT32 width)
{
	ACPI_TABLE_HEADER *	header = nullptr;

	ACPI_STATUS status = AcpiGetTable((char *)ACPI_SIG_MCFG, 0, &header);

	if (status != AE_OK || !header || !value)
		return false;

	auto const &mcfg = reinterpret_cast<ACPI_TABLE_MCFG const &>(*header);
	bool success     = false;

	using namespace Genode;

	with_config(bdf, mcfg, [&](auto & void_pci_config) {
		uint8_t * pci_config = reinterpret_cast<uint8_t *>(void_pci_config);
		*value = 0ull;
		if (width ==  8) *value = *(uint8_t  *)(pci_config + reg); else
		if (width == 16) *value = *(uint16_t *)(pci_config + reg); else
		if (width == 32) *value = *(uint32_t *)(pci_config + reg); else
		if (width == 64) *value = *(uint64_t *)(pci_config + reg); else {
			Genode::error("unsupported width=", width);
			return;
		}

		error("access ", bdf, " reg=", Hex(reg), " width=", width,
		      " value=", Hex(*value));
		success = true;

#if 0
		/* list PCI caps */
		error("pci_config[34]=", Hex(pci_config[0x34]));

		unsigned next_cap = *(uint16_t *)(pci_config + pci_config[0x34]);

		while ((next_cap >> 16) & 0xff) {
			uint8_t next = uint8_t((next_cap >> 16) & 0xff);
			log("cap ", next_cap & 0xff, " next=", Hex(next));
			next_cap = *(uint16_t *)(pci_config + next);
		}
#endif
	});

	return success;
}



static bool cpu_name(char const * name)
{
	unsigned cpuid = 0, edx = 0, ebx = 0, ecx = 0;
	asm volatile ("cpuid" : "+a" (cpuid), "=d" (edx), "=b"(ebx), "=c"(ecx));

	return ebx == *reinterpret_cast<unsigned const *>(name + 0) &&
	       edx == *reinterpret_cast<unsigned const *>(name + 4) &&
	       ecx == *reinterpret_cast<unsigned const *>(name + 8);
}


/*************************
 * Acpica PCI OS backend *
 *************************/

ACPI_STATUS AcpiOsInitialize (void) { return AE_OK; }

ACPI_STATUS AcpiOsReadPciConfiguration (ACPI_PCI_ID *pcidev, UINT32 reg,
                                        UINT64 *value, UINT32 width)
{
	using namespace Genode;

	Bdf bdf(pcidev->Bus, pcidev->Device, pcidev->Function);

	bool const intel   = cpu_name("GenuineIntel");
	bool const emulate = intel &&
	                     !pcidev->Bus && !pcidev->Device && !pcidev->Function;

	/*
	 * ACPI quirk for 12th Gen Framework laptop and Thinkpad X1 Nano Gen2
	 *
	 * XXX emulate some of the register accesses to the Intel root bridge to
	 *     avoid bogus calculation of physical addresses. The value seems to
	 *     be close to the pci config start address as provided by mcfg table
	 *     for those machines.
	 */
	if (emulate) {

		read_config(bdf, reg, value, width);

		if (reg == 0x60 && width == 32) {
			*value = 0xe0000001;
			warning(bdf, " emulate read ", Hex(reg), " -> ", Hex(*value));
			return AE_OK;
		}
	}

	/* during startup suppress errors */
	if (!(AcpiDbgLevel & ACPI_LV_INIT))
		error(__func__, " ", bdf, " ", Hex(reg), " width=", width);

	*value = ~0U;
	return AE_OK;
}

ACPI_STATUS AcpiOsWritePciConfiguration (ACPI_PCI_ID *pcidev, UINT32 reg,
                                         UINT64 value, UINT32 width)
{
	using namespace Genode;

	Bdf bdf(pcidev->Bus, pcidev->Device, pcidev->Function);

	/* during startup suppress errors */
	if (!(AcpiDbgLevel & ACPI_LV_INIT))
		error(__func__, " ", bdf, " ", Hex(reg), "=", Hex(value), " width=", width);

	return AE_OK;
}
