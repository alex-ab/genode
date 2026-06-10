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


class Pci
{
	private:

		static void for_each_sub_mcfg(ACPI_TABLE_HEADER *table, auto const &fn)
		{
			typedef ACPI_MCFG_ALLOCATION S;

			auto mcfg = reinterpret_cast<ACPI_TABLE_MCFG *>(table);

			for(S const * e = reinterpret_cast<S const * const>(mcfg + 1);
			    e < reinterpret_cast<S const *>(reinterpret_cast<char const *>(mcfg) + mcfg->Header.Length);
			    e = reinterpret_cast<S const *>(reinterpret_cast<char const *>(e) + sizeof(*e)))
			{
				fn(*e);
			}
		}

		static UINT32 _read_reg_32(Bdf const &bdf, UINT32 reg, void * base)
		{
			auto addr = reinterpret_cast<char *>(base)
			          + 32ul * 8ul * 0x1000ul * bdf.bus
			          +        8ul * 0x1000ul * bdf.dev
			          +              0x1000ul * bdf.fn
			          + reg;

			return *reinterpret_cast<UINT32 *>(addr);
		}

	public:

		static UINT32 read_pci_reg_32(Bdf const &bdf, UINT32 const reg)
		{
			UINT32              result = ~0U;
			ACPI_TABLE_HEADER * table  = nullptr;

			auto status = AcpiGetTable((char *)ACPI_SIG_MCFG, 0, &table);

			if (status != AE_OK)
				return result;

			for_each_sub_mcfg(table, [&](auto const &e) {

				if (bdf.bus < e.StartBusNumber || bdf.bus > e.EndBusNumber)
					return;

				auto phys_pci_cfg = e.Address
				                  + 256ul * 0x1000ul * (bdf.bus - e.StartBusNumber)
				                  +   8ul * 0x1000ul *  bdf.dev
				                  +         0x1000ul *  bdf.fn;

				auto virt_pci_cfg = AcpiOsMapMemory(phys_pci_cfg, 0x1000);

				if (!virt_pci_cfg)
					return;

				result = _read_reg_32(bdf, reg, virt_pci_cfg);

				AcpiOsUnmapMemory(virt_pci_cfg, 0x1000);
			});

			return result;
		}
};


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

	*value = ~0U;

	/*
	 * Read out some of the Intel root bridge register to avoid bogus io-mem
	 * address calculation, which are later on tried to be used and leading
	 * to red session denied messages.
	 */
	if (emulate) {
		/* name registers as specified by Intel for the root bridge */
		enum { MCHBAR = 0x48, PCIEXBAR = 0x60 };
		if (width == 32 && (reg == MCHBAR || reg == PCIEXBAR)) {

			*value = Pci::read_pci_reg_32(bdf, reg);

			log(bdf, " pci cfg read ", Hex(reg), " -> ", Hex(*value));

			return AE_OK;
		}
	}

	/* during startup suppress errors */
	if (!(AcpiDbgLevel & ACPI_LV_INIT))
		error(__func__, " ", bdf, " ", Hex(reg), " width=", width);

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
