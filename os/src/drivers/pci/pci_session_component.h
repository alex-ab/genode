/*
 * \brief  PCI-session component
 * \author Norman Feske
 * \date   2008-01-28
 */

/*
 * Copyright (C) 2008-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _PCI_SESSION_COMPONENT_H_
#define _PCI_SESSION_COMPONENT_H_

#include <base/rpc_server.h>
#include <pci_session/pci_session.h>
#include <root/component.h>

#include <io_mem_session/connection.h>

#include "pci_device_component.h"
#include "pci_config_access.h"

namespace Pci {

	/**
	 * Check if given PCI bus was found on inital scan
	 *
	 * This tremendously speeds up further scans by other drivers.
	 */
	bool bus_valid(int bus = 0)
	{
		struct Valid_buses
		{
			bool valid[Device_config::MAX_BUSES];

			void scan_bus(Config_access &config_access, int bus = 0)
			{
				for (int dev = 0; dev < Device_config::MAX_DEVICES; ++dev) {
					for (int fun = 0; fun < Device_config::MAX_FUNCTIONS; ++fun) {

						/* read config space */
						Device_config config(bus, dev, fun, &config_access);

						if (!config.valid())
							continue;

						/*
						 * There is at least one device on the current bus, so
						 * we mark it as valid.
						 */
						valid[bus] = true;

						/* scan behind bridge */
						if (config.is_pci_bridge()) {
							int sub_bus = config.read(&config_access,
							                          0x19, Device::ACCESS_8BIT);
							scan_bus(config_access, sub_bus);
						}
					}
				}
			}

			Valid_buses() { Config_access c; scan_bus(c); }
		};

		static Valid_buses buses;

		return buses.valid[bus];
	}

	class Session_component : public Genode::Rpc_object<Session>
	{
		private:

			Genode::Rpc_entrypoint         *_ep;
			Genode::Allocator              *_md_alloc;
			Genode::List<Device_component>  _device_list;

			/**
			 * Scan PCI busses for a device
			 *
			 * \param bus                start scanning at bus number
			 * \param device             start scanning at device number
			 * \param function           start scanning at function number
			 * \param out_device_config  device config information of the
			 *                           found device
			 * \param config_access      interface for accessing the PCI
			 *                           configuration
			 *                           space
			 *
			 * \retval true   device was found
			 * \retval false  no device was found
			 */
			bool _find_next(int bus, int device, int function,
			                Device_config *out_device_config,
			                Config_access *config_access)
			{
				for (; bus < Device_config::MAX_BUSES; bus++) {
					if (!bus_valid(bus))
						continue;

					for (; device < Device_config::MAX_DEVICES; device++) {
						for (; function < Device_config::MAX_FUNCTIONS; function++) {

							/* read config space */
							Device_config config(bus, device, function, config_access);

							if (config.valid()) {
								*out_device_config = config;
								return true;
							}
						}
						function = 0; /* init value for next device */
					}
					device = 0; /* init value for next bus */
				}
				return false;
			}

			/**
			 * List containing extended PCI config space information
			 */
			static Genode::List<Config_space> &config_space_list() {
				static Genode::List<Config_space> config_space;
				return config_space;
			}

			/**
			 * Find for a given PCI device described by the bus:dev:func triple
			 * the corresponding extended 4K PCI config space address.
			 * A io mem dataspace is created and returned.
			 */
			Genode::Io_mem_dataspace_capability
			lookup_config_space(Genode::uint8_t bus, Genode::uint8_t dev,
			                    Genode::uint8_t func)
			{
				using namespace Genode;

				uint32_t bdf = (bus << 8) | ((dev & 0x1f) << 3) | (func & 0x7);
				addr_t config_space = 0;

				Config_space *e = config_space_list().first();
				for (; e && !config_space; e = e->next())
					config_space = e->lookup_config_space(bdf);
		
				if (!config_space)
					return Io_mem_dataspace_capability();
						
				Io_mem_connection io_mem(config_space, 0x1000);
				io_mem.on_destruction(Io_mem_connection::KEEP_OPEN);
				return io_mem.dataspace();
			}

		public:

			/**
			 * Constructor
			 */
			Session_component(Genode::Rpc_entrypoint *ep,
			                  Genode::Allocator      *md_alloc):
				_ep(ep), _md_alloc(md_alloc) { }

			/**
			 * Destructor
			 */
			~Session_component()
			{
				/* release all elements of the session's device list */
				while (_device_list.first())
					release_device(_device_list.first()->cap());
			}

			static void add_config_space(Genode::uint32_t bdf_start,
			                             Genode::uint32_t func_count,
			                             Genode::addr_t base)
			{
				using namespace Genode;
				config_space_list().insert(new (env()->heap()) Config_space(bdf_start,
				                                                      func_count,
				                                                      base));
			}


			/***************************
			 ** PCI session interface **
			 ***************************/

			Device_capability first_device() {
				return next_device(Device_capability()); }

			Device_capability next_device(Device_capability prev_device)
			{
				/*
				 * Create the interface to the PCI config space.
				 * This involves the creation of I/O port sessions.
				 */
				Config_access config_access;

				/* lookup device component for previous device */
				Genode::Object_pool<Device_component>::Guard
					prev(_ep->lookup_and_lock(prev_device));

				/*
				 * Start bus scanning after the previous device's location.
				 * If no valid device was specified for 'prev_device', start at
				 * the beginning.
				 */
				int bus = 0, device = 0, function = 0;

				if (prev) {
					Device_config config = prev->config();
					bus      = config.bus_number();
					device   = config.device_number();
					function = config.function_number() + 1;
				}

				/*
				 * Scan busses for devices.
				 * If no device is found, return an invalid capability.
				 */
				Device_config config;
				if (!_find_next(bus, device, function, &config, &config_access))
					return Device_capability();

				/* get new bdf values */
				bus      = config.bus_number();
				device   = config.device_number();
				function = config.function_number();

				/* lookup if we have a extended pci config space */
				Genode::Io_mem_dataspace_capability cap;
				cap = lookup_config_space(bus, device, function);

				/*
				 * A device was found. Create a new device component for the
				 * device and return its capability.
				 *
				 * FIXME: check and adjust session quota
				 */
				Device_component *device_component =
					new (_md_alloc) Device_component(config, cap);

				if (!device_component)
					return Device_capability();

				_device_list.insert(device_component);
				return _ep->manage(device_component);
			}

			void release_device(Device_capability device_cap)
			{
				/* lookup device component for previous device */
				Device_component *device = dynamic_cast<Device_component *>
				                           (_ep->lookup_and_lock(device_cap));

				if (!device)
					return;

				_device_list.remove(device);
				_ep->dissolve(device);

				/* FIXME: adjust quota */
				destroy(_md_alloc, device);
			}

			Genode::Ram_dataspace_capability alloc_dma_buffer(Device_capability device_cap,
			                                                  Genode::size_t size)
			{
				return Genode::env()->ram_session()->alloc(size, false);
			}
	};


	class Root : public Genode::Root_component<Session_component>
	{
		private:

			Genode::Cap_session *_cap_session;

		protected:

			Session_component *_create_session(const char *args)
			{
				/* FIXME: extract quota from args */
				/* FIXME: pass quota to session-component constructor */

				return new (md_alloc()) Session_component(ep(), md_alloc());
			}

		public:

			/**
			 * Constructor
			 *
			 * \param ep        entry point to be used for serving the PCI session and
			 *                  PCI device interface
			 * \param md_alloc  meta-data allocator for allocating PCI-session
			 *                  components and PCI-device components
			 */
			Root(Genode::Rpc_entrypoint *ep,
			     Genode::Allocator      *md_alloc)
			:
				Genode::Root_component<Session_component>(ep, md_alloc)
			{
				/* enforce initial bus scan */
				bus_valid();
			}

			void parse_config()
			{
				static bool updated = false;

				/* only permitted once */
				if (updated)
					return;
				updated = true;

				using namespace Genode;

				try {
					unsigned i;
					for (i = 0; i < config()->xml_node().num_sub_nodes(); i++)
					{
						Xml_node node = config()->xml_node().sub_node(i);
						uint32_t bdf_start  = 0;
						uint32_t func_count = 0;
						addr_t   base       = 0;
						node.sub_node("start").value(&bdf_start);
						node.sub_node("count").value(&func_count);
						node.sub_node("base").value(&base);

						PINF("%2u BDF start %x, functions: 0x%x, physical base "
						     "0x%lx", i, bdf_start, func_count, base);

						Session_component::add_config_space(bdf_start,
						                                    func_count, base);
					}
				} catch (...) {
					PERR("PCI config space data could not be parsed.");
				}
			}
	};

}

#endif /* _PCI_SESSION_COMPONENT_H_ */
