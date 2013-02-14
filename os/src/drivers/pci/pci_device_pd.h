/*
 * \brief  General child PD handling for pci_drv
 * \author Alexander Boettcher
 * \date   2013-02-14
 */

/*
 * Copyright (C) 2013-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _PCI_DEVICE_PD_H_
#define _PCI_DEVICE_PD_H_

#include <base/env.h>

#include <init/child_policy.h>

#include <rm_session/connection.h>
#include <rom_session/connection.h>
#include <ram_session/connection.h>
#include <cpu_session/connection.h>
#include <cap_session/connection.h>

#include <io_mem_session/io_mem_session.h>
#include <util/touch.h>

#include "pci_device_pd_ipc.h"

namespace Pci {

class Pd_child_resources
{
	protected:

		Genode::Rom_connection _rom;
		Genode::Ram_connection _ram;
		Genode::Cpu_connection _cpu;
		Genode::Rm_connection  _rm;

		Pd_child_resources(const char *file_name, const char *name,
		                  Genode::size_t ram_quota)
		:
			_rom(file_name, name), _ram(name), _cpu(name)
		{
			_ram.ref_account(Genode::env()->ram_session_cap());
			Genode::env()->ram_session()->transfer_quota(_ram.cap(), ram_quota);
		}
};


class Pd_child : private Pd_child_resources,
                 public Genode::Child_policy,
                 private Init::Child_policy_enforce_labeling
{
	private:

		Genode::Service_registry *_parent_services;
		Genode::Child             _child;
		Pd_control_client         _pd_device_client;

		/* platform specific initialization */
		Genode::Capability<Pd_control> init(Genode::Cap_connection &cap_session);

	public:

		Pd_child(const char * file_name, const char * unique_name,
		         Genode::size_t ram_quota,
		         Genode::Service_registry * parent_service,
		         Genode::Rpc_entrypoint * ep,
		         Genode::Cap_connection &cap_session)
		:
			Pd_child_resources(file_name, unique_name, ram_quota),
			Init::Child_policy_enforce_labeling(unique_name),
			_parent_services(parent_service), 
			_child(_rom.dataspace(), _ram.cap(), _cpu.cap(), _rm.cap(), ep,
			       this),
			_pd_device_client(init(cap_session))
		{
		}

		const char *name() const { return "device pd"; }

		void filter_session_args(const char *, char *args,
		                         Genode::size_t args_len)
		{
			Child_policy_enforce_labeling::filter_session_args(0, args,
			                                                   args_len);
		}

		Genode::Service *resolve_session_request(const char *service_name,
		                                         const char *args)
		{
			return _parent_services->find(service_name);
		}

		void prepare_dma_mem(Genode::Ram_dataspace_capability ram_cap) {
			_pd_device_client.attach_dma_mem(ram_cap); }

		void assign_pci(Genode::Io_mem_dataspace_capability io_mem_cap) {
			_pd_device_client.assign_pci(io_mem_cap); }
};

}
#endif /* _PCI_DEVICE_PD_H_ */

