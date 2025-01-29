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

/* core includes */
#include <pd_session_component.h>
#include <assertion.h>

#include <novae_util.h> /* kernel_hip */

using namespace Core;

inline Novae::uint8_t retry_syscall(addr_t, auto const &fn)
{
	Novae::uint8_t res;

	res = fn();

	return res;
}

bool Pd_session_component::assign_pci(addr_t pci_config_memory, uint16_t bdf)
{
	return retry_syscall(_pd->pd_sel(), [&] {
		return Novae::assign_pci(_pd->pd_sel(), pci_config_memory, bdf);
	}) == Novae::NOVA_OK;
}


Pd_session::Map_result Pd_session_component::map(Pd_session::Virt_range const virt_range)
{
#if 0
	Platform_pd &target_pd = *_pd;
	Novae::Utcb  &utcb      = *reinterpret_cast<Novae::Utcb *>(Thread::myself()->utcb());
	addr_t const pd_core   = platform_specific().core_pd_sel();
	addr_t const pd_dst    = target_pd.pd_sel();
#endif

	auto map_memory = [&] (Mapping const &mapping)
	{
		error("Pd::map not implemented");
		uint8_t err = 1;
#if 0
		/* asynchronously map memory */
		uint8_t err = retry_syscall(_pd->pd_sel(), [&] {
			utcb.set_msg_word(0);

			bool res = utcb.append_item(nova_src_crd(mapping), 0, true, false,
			                            false,
			                            mapping.dma_buffer,
			                            mapping.write_combined);

			/* one item ever fits on the UTCB */
			(void)res;

			return Novae::delegate(pd_core, pd_dst, nova_dst_crd(mapping));
		});
#endif

		if (err != Novae::NOVA_OK) {
			error("could not eagerly map memory ",
			      Hex_range<addr_t>(mapping.dst_addr, 1UL << mapping.size_log2) , " "
			      "error=", err);
		}
	};

	addr_t virt = virt_range.start;
	size_t size = virt_range.num_bytes;
	try {
		while (size) {

			Fault const artificial_fault {
				.hotspot = { virt },
				.access  = Access::READ,
				.rwx     = Rwx::rwx(),
				.bounds  = { .start = 0, .end  = ~0UL },
			};

			_address_space.with_mapping_for_fault(artificial_fault,
				[&] (Mapping const &mapping)
				{
					map_memory(mapping);

					size_t const mapped_bytes = 1 << mapping.size_log2;

					virt += mapped_bytes;
					size  = size < mapped_bytes ? 0 : size - mapped_bytes;
				},

				[&] (Region_map_component &, Fault const &) { /* don't reflect */ }
			);
		}
	}
	catch (Out_of_ram)  { return Map_result::OUT_OF_RAM;   }
	catch (Out_of_caps) { return Map_result::OUT_OF_CAPS; }
	catch (...) {
		error(__func__, " failed ", Hex(virt), "+", Hex(size));
	}
	return Map_result::OK;
}


using State = Genode::Pd_session::Managing_system_state;


class System_control_component : public Genode::Rpc_object<Pd_session::System_control>
{
	public:

		State system_control(State const &) override;
};


class System_control_impl : public Core::System_control
{
	private:

		System_control_component objects [Core::Platform::MAX_SUPPORTED_CPUS] { };

		auto with_location(auto const &location, auto const &fn)
		{
			unsigned const index = platform_specific().pager_index(location);

			if (index < Core::Platform::MAX_SUPPORTED_CPUS)
				return fn (objects[index]);

			return Capability<Pd_session::System_control> { };
		}

		auto with_location(auto const &location, auto const &fn) const
		{
			unsigned const index = platform_specific().pager_index(location);

			if (index < Core::Platform::MAX_SUPPORTED_CPUS)
				return fn (objects[index]);

			return Capability<Pd_session::System_control> { };
		}

	public:

		Capability<Pd_session::System_control> control_cap(Affinity::Location const) const override;

		void manage(Rpc_entrypoint &ep, Affinity::Location const &location)
		{
			with_location(location, [&](auto &object) {
				ep.manage(&object);
				return object.cap();
			});
		}

};


static System_control_impl &system_instance()
{
	static System_control_impl system_control { };
	return system_control;
}


System_control & Core::init_system_control(Allocator &alloc, Rpc_entrypoint &)
{
	enum { ENTRYPOINT_STACK_SIZE = 20 * 1024 };

	platform_specific().for_each_location([&](Affinity::Location const &location) {

		auto ep = new (alloc) Rpc_entrypoint (nullptr, ENTRYPOINT_STACK_SIZE,
		                                      "system_control", location);

		system_instance().manage(*ep, location);
	});

	return system_instance();
};


Capability<Pd_session::System_control> System_control_impl::control_cap(Affinity::Location const location) const
{
	return with_location(location, [&](auto &object) {
		return object.cap();
	});
}


State System_control_component::system_control(State const &)
{
	return State();
}
