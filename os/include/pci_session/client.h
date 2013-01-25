/*
 * \brief  Client-side PCI-session interface
 * \author Norman Feske
 * \date   2008-01-28
 */

/*
 * Copyright (C) 2008-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _INCLUDE__PCI_SESSION__CLIENT_H_
#define _INCLUDE__PCI_SESSION__CLIENT_H_

#include <pci_session/capability.h>
#include <base/rpc_client.h>

namespace Pci {

	struct Session_client : public Genode::Rpc_client<Session>
	{
		Session_client(Session_capability session)
		: Genode::Rpc_client<Session>(session) { }

		Device_capability first_device() {
			return call<Rpc_first_device>(); }

		Device_capability next_device(Device_capability prev_device) {
			return call<Rpc_next_device>(prev_device); }

		void release_device(Device_capability device) {
			call<Rpc_release_device>(device); }

		void set_config_extended(Device_capability device_cap,
		                         Genode::Io_mem_dataspace_capability io_cap) {
			call<Rpc_set_config_extended>(device_cap, io_cap); }

		Genode::Ram_dataspace_capability alloc_dma_mem(Device_capability device_cap,
		                                               Genode::size_t size) {
			return call<Rpc_alloc_dma_mem>(device_cap, size); }
	};
}

#endif /* _INCLUDE__PCI_SESSION__CLIENT_H_ */
