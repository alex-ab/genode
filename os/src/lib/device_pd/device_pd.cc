/*
 * \brief  dummy PCI device pd handling
 * \author Alexander Boettcher
 * \date   2013-02-14
 */

/*
 * Copyright (C) 2013-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#include "../src/drivers/pci/pci_device_pd.h"

namespace Pci {
	Genode::Capability<Pd_control> Pd_child::init(Genode::Cap_connection &cap_session) {
			return Genode::reinterpret_cap_cast<Pd_control>(Genode::Native_capability()); }
}

