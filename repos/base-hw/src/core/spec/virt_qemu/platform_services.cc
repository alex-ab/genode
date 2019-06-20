/*
 * \brief   Platform specific services for Virt Qemu
 * \author  Alexander Boettcher
 * \date    2019-06-20
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/service.h>
#include <base/heap.h>

/* core includes */
#include <platform.h>
#include <platform_services.h>
#include <core_env.h>
#include <core_service.h>
#include <map_local.h>
#include <vm_root.h>
#include <platform.h>

extern Genode::addr_t el2_exception_vector;
extern Genode::addr_t vt_host_context;

/*
 * Add ARM 64 virtualization specific vm service
 */
void Genode::platform_add_local_services(Rpc_entrypoint         &ep,
                                         Sliced_heap            &sh,
                                         Registry<Service>      &services,
                                         Trace::Source_registry &trace_sources)
{
	using namespace Genode;

	log( "v=", Hex((addr_t)&el2_exception_vector),
	    " p=", Hex(Platform::core_phys_addr((addr_t)&el2_exception_vector)),
	    " -> ", Hex(Hw::Mm::hypervisor_exception_vector().base),
	    "+", Hex(Hw::Mm::hypervisor_exception_vector().size),
	    " el2 exception vector");

	if (!map_local(Platform::core_phys_addr((addr_t)&el2_exception_vector),
	               Hw::Mm::hypervisor_exception_vector().base,
	               Hw::Mm::hypervisor_exception_vector().size / 0x1000,
	               Hw::PAGE_FLAGS_KERN_TEXT)) {
		error("mapping exception vector failed");
		while (1) { }
	}

	log( "v=", Hex((addr_t)&vt_host_context & ~0xfffull),
	    " p=", Hex(Platform::core_phys_addr((addr_t)&vt_host_context)),
	    " -> ", Hex(Hw::Mm::hypervisor_host_context().base),
	    "+", Hex(Hw::Mm::hypervisor_host_context().size),
	    " el2/el1 host context");

	if (!map_local(Platform::core_phys_addr((addr_t)&vt_host_context) & ~0xFFFULL,
	               Hw::Mm::hypervisor_host_context().base,
	               Hw::Mm::hypervisor_host_context().size / 0x1000,
	               Hw::PAGE_FLAGS_KERN_TEXT)) {
		error("mapping host context failed");
		while (1) { }
	}

	static Vm_root vm_root(ep, sh, core_env().ram_allocator(),
	                       core_env().local_rm(), trace_sources);
	static Core_service<Vm_session_component> vm_service(services, vm_root);
}
