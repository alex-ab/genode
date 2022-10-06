/*
 * \brief  Extension of core implementation of the PD session interface
 * \author Alexander Boettcher
 * \date   2013-01-11
 */

/*
 * Copyright (C) 2013-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core */
#include <pd_session_component.h>
#include <assertion.h>

using namespace Genode;

template <typename FUNC>
inline Nova::uint8_t retry_syscall(addr_t pd_sel, FUNC func)
{
	Nova::uint8_t res;
	do {
		res = func();
	} while (res == Nova::NOVA_PD_OOM &&
	         Nova::NOVA_OK == Pager_object::handle_oom(Pager_object::SRC_CORE_PD,
	                                                   pd_sel,
	                                                   "core", "ep",
	                                                   Pager_object::Policy::UPGRADE_CORE_TO_DST));

	return res;
}

bool Pd_session_component::assign_pci(addr_t pci_config_memory, uint16_t bdf)
{
	return retry_syscall(_pd->pd_sel(), [&]() {
		return Nova::assign_pci(_pd->pd_sel(), pci_config_memory, bdf);
	}) == Nova::NOVA_OK;
}


void Pd_session_component::map(addr_t virt, addr_t size)
{
	Genode::addr_t const  pd_core   = platform_specific().core_pd_sel();
	Platform_pd          &target_pd = *_pd;
	Genode::addr_t const  pd_dst    = target_pd.pd_sel();
	Nova::Utcb           &utcb      = *reinterpret_cast<Nova::Utcb *>(Thread::myself()->utcb());

	auto lambda = [&] (Region_map_component *region_map,
	                   Rm_region            *region,
	                   addr_t const          ds_offset,
	                   addr_t const          region_offset,
	                   addr_t const          dst_region_size) -> addr_t
	{
		Dataspace_component * dsc = region ? &region->dataspace() : nullptr;
		if (!dsc) {
			struct No_dataspace{};
			throw No_dataspace();
		}
		if (!region_map) {
			ASSERT_NEVER_CALLED;
		}

		Mapping mapping = Region_map_component::create_map_item(region_map,
		                                                        *region,
		                                                        ds_offset,
		                                                        region_offset,
		                                                        *dsc, virt,
		                                                        dst_region_size);

		/* asynchronously map memory */
		uint8_t err = retry_syscall(_pd->pd_sel(), [&]() {
			utcb.set_msg_word(0);

			bool res = utcb.append_item(nova_src_crd(mapping), 0, true, false,
			                            false,
			                            mapping.dma_buffer,
			                            mapping.write_combined);

			/* one item ever fits on the UTCB */
			(void)res;

			return Nova::delegate(pd_core, pd_dst, nova_dst_crd(mapping));
		});

		addr_t const map_size = 1UL << mapping.size_log2;
		addr_t const mapped = mapping.dst_addr + map_size - virt;

		if (err != Nova::NOVA_OK) {
			error("could not map memory ",
			      Hex_range<addr_t>(mapping.dst_addr, map_size) , " "
			      "eagerly error=", err);
		}

		return mapped;
	};

	try {
		while (size) {
			addr_t mapped = _address_space.apply_to_dataspace(virt, lambda);
			virt         += mapped;
			size          = size < mapped ? size : size - mapped;
		}
	} catch (...) {
		error(__func__, " failed ", Hex(virt), "+", Hex(size));
	}
}


using State = Genode::Pd_session::Managing_system_state;

static State acpi_suspend(State const &request)
{
	State respond { .trapno = 0 };

	/*
	 * The trapno/ip/sp registers used below are just convention to transfer
	 * the intended sleep state S0 ... S5. The values are read out by an
	 * ACPI AML component and are of type TYP_SLPx as described in the
	 * ACPI specification, e.g. TYP_SLPa and TYP_SLPb. The values differ
	 * between different PC systems/boards.
	 *
	 * \note trapno/ip/sp registers are chosen because they exist in
	 *       Managing_system_state for x86_32 and x86_64.
	 */
	uint8_t const sleep_type_a = uint8_t(request.ip);
	uint8_t const sleep_type_b = uint8_t(request.sp);

	auto const cap_suspend = platform_specific().core_pd_sel() + 3;
	auto const result      = Nova::acpi_suspend(cap_suspend, sleep_type_a,
	                                            sleep_type_b);

	if (result == Nova::NOVA_OK)
		respond.trapno = 1 /* success, which means we resumed already */;

	return respond;
}


static State msr_access_cap(State const &, Platform_pd &target_pd)
{
	enum { SM_MSR = 0x20 };  /* convention */

	Genode::addr_t const  pd_core   = platform_specific().core_pd_sel();
	Genode::addr_t const  pd_dst    = target_pd.pd_sel();
	Nova::Utcb           &utcb      = *reinterpret_cast<Nova::Utcb *>(Thread::myself()->utcb());

	unsigned const msr_cap = platform_specific().core_pd_sel() + 4;

	Nova::Obj_crd src_crd(msr_cap, 0 /* order */);
	Nova::Obj_crd dst_crd(SM_MSR , 0 /* order */);

	retry_syscall(target_pd.pd_sel(), [&]() {
		utcb.set_msg_word(0);
		bool res = utcb.append_item(src_crd, 0 /* hotspot */, true /* kernel pd */);
		/* one item ever fits on the UTCB */
		(void)res;
		return Nova::delegate(pd_core, pd_dst, dst_crd);
	});

	return State();
}


State Pd_session_component::managing_system(State const &request)
{
	if (_managing_system != Managing_system::PERMITTED) {
		return State();
	}

	if (request.trapno == State::ACPI_SUSPEND_REQUEST)
		return acpi_suspend(request); 

	if (request.trapno == State::ACPI_SUSPEND_REQUEST + 1 /* XXX */)
		return msr_access_cap(request, *_pd);

	return State();
}
