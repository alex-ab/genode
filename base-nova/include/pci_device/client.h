/*
 * \brief  Client-side interface for PCI device
 * \author Norman Feske
 * \author Alexander Boettcher
 * \date   2008-01-28
 */

/*
 * Copyright (C) 2008-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _INCLUDE__PCI_DEVICE__CLIENT_H_
#define _INCLUDE__PCI_DEVICE__CLIENT_H_

#include <pci_session/pci_session.h>
#include <pci_device/pci_device.h>
#include <base/rpc_client.h>
#include <io_mem_session/io_mem_session.h>
#include <pd_session/client.h>

#include <util/touch.h>

extern "C" bool genode_iommu;

namespace Pci {

	struct Device_client : public Genode::Rpc_client<Device>
	{
		Device_client(Device_capability device)
		: Genode::Rpc_client<Device>(device) {

			using namespace Genode;

			Genode::Io_mem_dataspace_capability config_cap;
			config_cap = config_extended();

			if (!config_cap.valid())
				return;

			void * config_space = env()->rm_session()->attach(config_cap);
			if (!config_space)
				return;
			
			touch_read(reinterpret_cast<unsigned char const *>(config_space));

			Genode::Pd_session_client client(Genode::env()->pd_session_cap());
			bool res = client.assign_pci(reinterpret_cast<addr_t>(config_space));
			if (!res)
				PWRN("assignment of pci to driver task failed");

			/* set iommu as on */
			if (!genode_iommu && res)
				genode_iommu = true;

			env()->rm_session()->detach(config_space);
 		}

		void bus_address(unsigned char *bus, unsigned char *dev, unsigned char *fn) {
			call<Rpc_bus_address>(bus, dev, fn); }

		unsigned short vendor_id() {
			return call<Rpc_vendor_id>(); }

		unsigned short device_id() {
			return call<Rpc_device_id>(); }

		unsigned class_code() {
			return call<Rpc_class_code>(); }

		Resource resource(int resource_id) {
			return call<Rpc_resource>(resource_id); }

		unsigned config_read(unsigned char address, Access_size size) {
			return call<Rpc_config_read>(address, size); }

		void config_write(unsigned char address, unsigned value, Access_size size) {
			call<Rpc_config_write>(address, value, size); }

		Genode::Io_mem_dataspace_capability config_extended() {
			return call<Rpc_config_extended>(); }
	};
}

#endif /* _INCLUDE__PCI_DEVICE__CLIENT_H_ */
