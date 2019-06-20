/*
 * \brief   Platform implementations specific for base-hw and Raspberry Pi3
 * \author  Stefan Kalkowski
 * \date    2019-05-11
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <platform.h>

using Bootstrap::Cpu;


/**
 * Leave out the first page (being 0x0) from bootstraps RAM allocator,
 * some code does not feel happy with addresses being zero
 */
Bootstrap::Platform::Board::Board()
: early_ram_regions(Memory_region { ::Board::RAM_BASE + 0x1000,
                                    ::Board::RAM_SIZE - 0x1000 }),
  late_ram_regions(Memory_region { ::Board::RAM_BASE, 0x1000 }),
  core_mmio(Memory_region { ::Board::UART_BASE, ::Board::UART_SIZE },
            Memory_region { ::Board::Cpu_mmio::IRQ_CONTROLLER_DISTR_BASE,
                            ::Board::Cpu_mmio::IRQ_CONTROLLER_DISTR_SIZE },
            Memory_region { ::Board::Cpu_mmio::IRQ_CONTROLLER_CPU_BASE,
                            ::Board::Cpu_mmio::IRQ_CONTROLLER_CPU_SIZE }) {}


static inline void prepare_non_secure_world()
{
	bool el2 = Cpu::Id_pfr0::El2::get(Cpu::Id_pfr0::read());

	Cpu::Scr::access_t scr = Cpu::Scr::read();
	Cpu::Scr::Ns::set(scr,  1); /* set non-secure bit */
	Cpu::Scr::Rw::set(scr,  1); /* exec in aarch64    */
	Cpu::Scr::Smd::set(scr, 1); /* disable smc call   */
	Cpu::Scr::write(scr);

	Cpu::Spsr::access_t pstate = 0;
	Cpu::Spsr::Sp::set(pstate, 1); /* select non-el0 stack pointer */
	Cpu::Spsr::El::set(pstate, el2 ? Cpu::Current_el::EL2
	                               : Cpu::Current_el::EL1);
	Cpu::Spsr::F::set(pstate, 1);
	Cpu::Spsr::I::set(pstate, 1);
	Cpu::Spsr::A::set(pstate, 1);
	Cpu::Spsr::D::set(pstate, 1);
	Cpu::Spsr_el3::write(pstate);

#ifndef SWITCH_TO_ELX
#define SWITCH_TO_ELX(el)   \
	"mov x0, sp \n"         \
	"msr sp_" #el ", x0 \n" \
	"adr x0, 1f \n"         \
	"msr elr_el3, x0 \n"    \
	"eret \n"               \
	"1:"

	if (el2)
		asm volatile(SWITCH_TO_ELX(el2) ::: "x0");
	else
		asm volatile(SWITCH_TO_ELX(el1) ::: "x0");
#undef SWITCH_TO_ELX
#else
#error "macro SWITCH_TO_ELX already defined"
#endif
}

static inline void prepare_hypervisor(Cpu::Ttbr::access_t const ttbr)
{
	Cpu::Hcr_el2::access_t scr = Cpu::Hcr_el2::read();
	Cpu::Hcr_el2::Rw::set(scr,  1); /* exec in aarch64 */
	Cpu::Hcr_el2::Tlor::set(scr, 1); /* trap lorc_el1 and co - not saved/restored on VM switch */
	Cpu::Hcr_el2::write(scr);

	/* set hypervisor exception vector */
	Cpu::Vbar_el2::write(Hw::Mm::hypervisor_exception_vector().base);
	Genode::addr_t const stack_el2 = Hw::Mm::hypervisor_stack().base +
	                                 Hw::Mm::hypervisor_stack().size - 0x10; /* XXX */

	/* set hypervisor's translation table */
	Cpu::Ttbr0_el2::write(ttbr);

	Cpu::Tcr_el2::access_t tcr_el2 = 0;
	Cpu::Tcr_el2::T0sz::set(tcr_el2, 25);
	Cpu::Tcr_el2::Irgn0::set(tcr_el2, 1);
	Cpu::Tcr_el2::Orgn0::set(tcr_el2, 1);
	Cpu::Tcr_el2::Sh0::set(tcr_el2, 0b10);

	/* prepare MMU usage by hypervisor code */
	Cpu::Tcr_el2::write(tcr_el2);

	/* set memory attributes in indirection register */
	Cpu::Mair_el2::access_t mair = 0;
	Cpu::Mair_el2::Attr0::set(mair, Cpu::Mair_el2::NORMAL_MEMORY_UNCACHED);
	Cpu::Mair_el2::Attr1::set(mair, Cpu::Mair_el2::DEVICE_MEMORY);
	Cpu::Mair_el2::Attr2::set(mair, Cpu::Mair_el2::NORMAL_MEMORY_CACHED);
	Cpu::Mair_el2::Attr3::set(mair, Cpu::Mair_el2::DEVICE_MEMORY);
	Cpu::Mair_el2::write(mair);

	Cpu::Vtcr_el2::access_t vtcr = 0;
	Cpu::Vtcr_el2::T0sz::set(vtcr, 25);
	Cpu::Vtcr_el2::Sl0::set(vtcr, 1); /* set to starting level 1 */
	Cpu::Vtcr_el2::write(vtcr);

	Cpu::Spsr::access_t pstate = 0;
	Cpu::Spsr::Sp::set(pstate, 1); /* select non-el0 stack pointer */
	Cpu::Spsr::El::set(pstate, Cpu::Current_el::EL1);
	Cpu::Spsr::F::set(pstate, 1);
	Cpu::Spsr::I::set(pstate, 1);
	Cpu::Spsr::A::set(pstate, 1);
	Cpu::Spsr::D::set(pstate, 1);
	Cpu::Spsr_el2::write(pstate);

	Cpu::Sctlr_el2::access_t sctlr = Cpu::Sctlr_el2::read();
	Cpu::Sctlr_el2::M::set(sctlr, 1);
	Cpu::Sctlr_el2::A::set(sctlr, 0);
	Cpu::Sctlr_el2::C::set(sctlr, 1);
	Cpu::Sctlr_el2::Sa::set(sctlr, 0);
	Cpu::Sctlr_el2::I::set(sctlr, 1);
	Cpu::Sctlr_el2::Wxn::set(sctlr, 0);
	Cpu::Sctlr_el2::write(sctlr);

	Cpu::Mdcr_el2::access_t mdcr = Cpu::Mdcr_el2::read();
	/* disable debug registers - registers not saved/restored in EL2 */
	Cpu::Mdcr_el2::Tda::set(mdcr, 1);
	Cpu::Mdcr_el2::Tdosa::set(mdcr, 1);
	/* disable performance counter registers - not saved/restored by EL2 */
	Cpu::Mdcr_el2::Tpm::set(mdcr, 1);
	Cpu::Mdcr_el2::Tpmcr::set(mdcr, 1);
	Cpu::Mdcr_el2::write(mdcr);

	asm volatile(
	             "mov x0, sp      \n"
	             "msr sp_el1, x0  \n"
	             "adr x0, 1f      \n"
	             "msr elr_el2, x0 \n"
	             "mov sp, %0      \n"
	             "eret            \n"
	             "1:": : "r"(stack_el2): "x0");
}


unsigned Bootstrap::Platform::enable_mmu()
{
	Cpu::Ttbr::access_t ttbr = Cpu::Ttbr::Baddr::masked((Genode::addr_t)core_pd->table_base);

	while (Cpu::current_privilege_level() > Cpu::Current_el::EL1) {
		if (Cpu::current_privilege_level() == Cpu::Current_el::EL3)
			prepare_non_secure_world();
		else
			prepare_hypervisor(ttbr);
	}

	Cpu::Vbar_el1::write(Hw::Mm::supervisor_exception_vector().base);

	/* set memory attributes in indirection register */
	Cpu::Mair::access_t mair = 0;
	Cpu::Mair::Attr0::set(mair, Cpu::Mair::NORMAL_MEMORY_UNCACHED);
	Cpu::Mair::Attr1::set(mair, Cpu::Mair::DEVICE_MEMORY);
	Cpu::Mair::Attr2::set(mair, Cpu::Mair::NORMAL_MEMORY_CACHED);
	Cpu::Mair::Attr3::set(mair, Cpu::Mair::DEVICE_MEMORY);
	Cpu::Mair::write(mair);

	Cpu::Ttbr0_el1::write(ttbr);
	Cpu::Ttbr1_el1::write(ttbr);

	Cpu::Tcr_el1::access_t tcr = 0;
	Cpu::Tcr_el1::T0sz::set(tcr, 25);
	Cpu::Tcr_el1::T1sz::set(tcr, 25);
	Cpu::Tcr_el1::Irgn0::set(tcr, 1);
	Cpu::Tcr_el1::Irgn1::set(tcr, 1);
	Cpu::Tcr_el1::Orgn0::set(tcr, 1);
	Cpu::Tcr_el1::Orgn1::set(tcr, 1);
	Cpu::Tcr_el1::Sh0::set(tcr, 0b10);
	Cpu::Tcr_el1::Sh1::set(tcr, 0b10);
	Cpu::Tcr_el1::Ips::set(tcr, 0b10);
	Cpu::Tcr_el1::As::set(tcr, 1);
	Cpu::Tcr_el1::write(tcr);

	Cpu::Sctlr_el1::access_t sctlr = Cpu::Sctlr_el1::read();
	Cpu::Sctlr_el1::C::set(sctlr, 1);
	Cpu::Sctlr_el1::A::set(sctlr, 0);
	Cpu::Sctlr_el1::M::set(sctlr, 1);
	Cpu::Sctlr_el1::Sa0::set(sctlr, 1);
	Cpu::Sctlr_el1::Sa::set(sctlr, 0);
	Cpu::Sctlr_el1::write(sctlr);

	return 0;
}
