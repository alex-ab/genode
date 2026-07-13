/*
 * \brief  Core implementation of the PD session interface
 * \author Alexander Boettcher
 * \author Stefan Kalkowski
 * \date   2022-12-02
 */

/*
 * Copyright (C) 2022-2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <cpu/cpu_state.h>
#include <pd_session_component.h>
#include <spec/x86_64/cpu.h>

using namespace Core;
using State = Genode::Pd_session::Managing_system_state;


struct System_control_component : Genode::Rpc_object<Pd_session::System_control>
{
	State system_control(State const &) override;
};


class Hw_system_control : public Core::System_control
{
	private:

		struct Per_cpu : Registry<Per_cpu>::Element
		{
			Rpc_entrypoint ep;
			System_control_component obj {};
			int const cpu;

			Per_cpu(Registry<Per_cpu> &, Runtime &, int);
		};

		Registry<Per_cpu> _registry {};
		Memory::Constrained_obj_allocator<Per_cpu> _alloc;

	public:

		Hw_system_control(Runtime &, Allocator &);

		Capability<Pd_session::System_control>
		control_cap(Affinity::Location const) const override;
};


State System_control_component::system_control(State const &request)
{
	State respond { };

	switch (request.trapno) {
	case State::ACPI_SUSPEND_REQUEST:
		{
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
			unsigned const sleep_type_a = request.ip & 0xffu;
			unsigned const sleep_type_b = request.sp & 0xffu;

			respond.trapno = (Kernel::cpu_suspend((sleep_type_b << 8) | sleep_type_a)
			                  == Kernel::Cpu_suspend_result::OK) ? 1 : 0;
			break;
		}
	case State::MSR_ACCESS:
		{
			auto get = [] (State const &s, unsigned idx) {
				switch (idx) {
				case 0: return s.r8;
				case 1: return s.r9;
				case 2: return s.r10;
				case 3: return s.r11;
				case 4: return s.r12;
				case 5: return s.r13;
				case 6: return s.r14;
				case 7: return s.r15;
				default: ;
				};
				return 0UL;
			};

			auto set = [] (State &s, unsigned idx, addr_t value) {
				switch (idx) {
				case 0: s.r8  = value; return;
				case 1: s.r9  = value; return;
				case 2: s.r10 = value; return;
				case 3: s.r11 = value; return;
				case 4: s.r12 = value; return;
				case 5: s.r13 = value; return;
				case 6: s.r14 = value; return;
				case 7: s.r15 = value; return;
				default: ;
				};
			};

			static constexpr uint64_t write_mask = 1UL << 29;

			for (unsigned i = 0, op = 0; i < min(request.ip, 8UL); i++, op++) {
				auto msr = get(request, i);
				bool write = (msr & write_mask);
				addr_t v;
				auto ret = write ? Kernel::sys_reg_write(msr & ~write_mask,
				                                         get(request, ++i))
				                 : Kernel::sys_reg_read(msr, v);

				if (ret != Kernel::Sys_reg_access_result::OK)
					continue;

				if (!write) set(respond, op, v);
				respond.ip |= 1 << op;
			}
			respond.trapno = 1;
			break;
		}
	default:
		/* report failed attempt */
		respond.trapno = 0;
	}

	return respond;
}


Hw_system_control::Per_cpu::Per_cpu(Registry<Per_cpu> &registry,
                                    Runtime &runtime, int idx)
:
	Registry<Per_cpu>::Element(registry, *this),
	ep(runtime, "system_control", Thread::Stack_size{12*1024},
	   Affinity::Location{idx, 0}),
	cpu(idx)
{
	ep.manage(&obj);
}


Hw_system_control::Hw_system_control(Runtime &runtime, Allocator &alloc)
:
	_alloc(alloc)
{
	for (int i = 0; i < (int)platform().affinity_space().total(); i++)
		_alloc.create(_registry, runtime, i).with_result(
			[] (auto &a) { a.deallocate = false; },
			[&] (auto) {
				error("Could not create system control ep for cpu ", i);
		});
}


System_control & Core::init_system_control(Runtime &runtime, Allocator &alloc,
                                           Rpc_entrypoint &)
{
	static Hw_system_control system_control { runtime, alloc };
	return system_control;
}


Capability<Pd_session::System_control>
Hw_system_control::control_cap(Affinity::Location const location) const
{
	Capability<Pd_session::System_control> result {};

	_registry.for_each([&] (auto &per_cpu) {
		if (location.xpos() == per_cpu.cpu) result = per_cpu.obj.cap(); });

	return result;
}


/***************************
 ** Dummy implementations **
 ***************************/

bool Pd_session_component::assign_pci(addr_t, uint16_t) { return true; }


Pd_session::Map_result Pd_session_component::map(Pd_session::Virt_range) { return Map_result::OK; }
