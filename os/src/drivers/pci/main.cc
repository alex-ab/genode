/*
 * \brief  PCI-bus driver
 * \author Norman Feske
 * \date   2008-01-28
 */

/*
 * Copyright (C) 2008-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#include <base/env.h>
#include <base/printf.h>
#include <cap_session/connection.h>

#include <os/config.h>

#include "pci_session_component.h"
#include "pci_device_config.h"

using namespace Genode;
using namespace Pci;

int main(int argc, char **argv)
{
	printf("PCI driver started\n");

	/*
	 * Initialize server entry point
	 */
	enum { STACK_SIZE = 2 * sizeof(addr_t)*1024 };
	static Cap_connection cap;
	static Rpc_entrypoint ep(&cap, STACK_SIZE, "pci_ep");

	/*
	 * Use sliced heap to allocate each session component at a separate
	 * dataspace.
	 */
	static Sliced_heap sliced_heap(env()->ram_session(), env()->rm_session());

	/* register signal handler for config changes */
	Signal_receiver sig_rec;
	Signal_context sig_ctx;
	try {
		config()->sigh(sig_rec.manage(&sig_ctx));
	} catch (Genode::Config::Invalid) {
		PERR("Error: could not register for config file updates.");
	}

	/*
	 * Let the entry point serve the PCI root interface
	 */
	static Pci::Root root(&ep, &sliced_heap);
	env()->parent()->announce(ep.manage(&root));

	for (;;) {

		/* wait for config change */
		sig_rec.wait_for_signal();

		try {
			config()->reload();
			root.parse_config();
		} catch (Genode::Config::Invalid) {
			PERR("Error: reloading config failed");
		}
	}

	return 1;
}
