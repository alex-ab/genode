/*
 * \brief  PCI device pd handling 
 * \author Alexander Boettcher
 * \date   2013-02-14
 *
 */

/*
 * Copyright (C) 2013-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#include <base/printf.h>
#include <base/elf.h>

#include "../src/drivers/pci/pci_device_pd.h"

namespace Pci {

	static Genode::addr_t elf_entry(Genode::Dataspace_capability rom_cap)
	{
		using namespace Genode;

		/* attach ELF locally */
		addr_t elf_addr;
		addr_t elf_entry = 0;

		try { elf_addr = env()->rm_session()->attach(rom_cap); }
		catch (Rm_session::Attach_failed) { return elf_entry; }

		/* setup ELF object and read program entry pointer */
		Elf_binary elf((addr_t)elf_addr);
		if (elf.valid())
			elf_entry = elf.entry();

			env()->rm_session()->detach((void *)elf_addr);

		return elf_entry;
	}


	Genode::Capability<Pd_control> Pd_child::init(Genode::Cap_connection &cap_session)
	{
		using namespace Genode;

		Native_capability ec_cap = _cpu.native_cap(_child.main_thread_cap());
		addr_t ip = elf_entry(_rom.dataspace());

		printf("ready %lx native ec thread cap, entry ip = %lx\n",
		       ec_cap.local_name(), ip);

		Native_capability pd_service_cap = cap_session.alloc(ec_cap, ip);

		printf("ready %lx communiation portal id\n", pd_service_cap.local_name());

		Nova::Utcb *utcb = (Nova::Utcb *)Thread_base::myself()->utcb();

		utcb->items = 0;
		uint8_t res = Nova::call(pd_service_cap.local_name());
		printf("init call to worker %u\n", res);

		return reinterpret_cap_cast<Pd_control>(pd_service_cap);
	}

}
