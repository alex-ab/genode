/*
 * \brief   VMCB data structure
 * \author  Benjamin Lamowski
 * \author  Stefan Kalkowski
 * \date    2022-12-21
 */

/*
 * Copyright (C) 2022-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__SPEC__PC__SVM_H_
#define _INCLUDE__SPEC__PC__SVM_H_

#include <base/internal/page_size.h>
#include <base/stdint.h>
#include <cpu.h>
#include <cpu/vcpu_state.h>
#include <spec/x86_64/virt_interface.h>
#include <util/mmio.h>
#include <util/string.h>

namespace Board
{
	using namespace Genode;

	struct Vmcb;
	struct Vmcb_buf;
}


/*
 * VMCB data structure
 * See: AMD Manual Vol. 2, Appendix B Layout of VMCB
 */
struct Board::Vmcb_buf : public Mmio<PAGE_SIZE>
{
	struct Intercept_ex : Register<0x8, 32>
	{
		struct Ac : Bitfield<17,1> { };
	};

	struct Intercept_misc1 : Register<0xc, 32>
	{
		struct Intr      : Bitfield< 0, 1> { };
		struct Nmi       : Bitfield< 1, 1> { };
		struct Init      : Bitfield< 3, 1> { };
		struct Vintr     : Bitfield< 4, 1> { };
		struct Invd      : Bitfield<22, 1> { };
		struct Hlt       : Bitfield<24, 1> { };
		struct Ioio_prot : Bitfield<27, 1> { };
		struct Msr_prot  : Bitfield<28, 1> { };
		struct Shutdown  : Bitfield<31, 1> { };
	};

	struct Intercept_misc2 : Register<0x10, 32>
	{
		struct Vmload : Bitfield< 2, 1> { };
		struct Vmsave : Bitfield< 3, 1> { };
		struct Clgi   : Bitfield< 5, 1> { };
		struct Skinit : Bitfield< 6, 1> { };
	};

	struct Iopm_base_pa  : Register<0x40, 64> { };
	struct Msrpm_base_pa : Register<0x48, 64> { };
	struct Tsc_offset    : Register<0x50, 64> { };

	struct Tlb : Register<0x58, 64>
	{
		struct Guest_asid : Bitfield<0, 32> {};
	};

	struct Int_control : Register<0x60, 64>
	{
		struct V_tpr       : Bitfield< 0, 8> { };
		struct V_irq       : Bitfield< 8, 1> { };
		struct V_ign_tpr   : Bitfield<20, 1> { };
		struct V_intr_mask : Bitfield<24, 1> { };
	};

	struct Int_control_ext : Register<0x68, 64>
	{
		struct Int_shadow : Bitfield<0, 1> { };
	};

	struct Exitcode    : Register<0x70, 64> { };
	struct Exitinfo1   : Register<0x78, 64> { };
	struct Exitinfo2   : Register<0x80, 64> { };
	struct Exitintinfo : Register<0x88, 64> { };

	struct Npt_control : Register<0x90, 64>
	{
		struct Np_enable : Bitfield<0, 1> { };
	};

	struct Eventinj : Register<0xa8, 64> { };
	struct N_cr3    : Register<0xb0, 64> { };

	struct Vmsa : Register<0x108,64>
	{
		struct Vmsa_ptr : Bitfield<12, 40> { };
	};

	static constexpr size_t STATE_OFF = 0x400;

	/*
	 * Segments are 128bit in size and therefore cannot be represented with
	 * the current Register Framework.
	 */
	struct Segment : public Mmio<128>
	{
		using Mmio<128>::Mmio;

		struct Sel   : Register<0x0,16> { };
		struct Ar    : Register<0x2,16> { };
		struct Limit : Register<0x4,32> { };
		struct Base  : Register<0x8,64> { };
	};

	Segment   es { range_at(STATE_OFF +  0x0) };
	Segment   cs { range_at(STATE_OFF + 0x10) };
	Segment   ss { range_at(STATE_OFF + 0x20) };
	Segment   ds { range_at(STATE_OFF + 0x30) };
	Segment   fs { range_at(STATE_OFF + 0x40) };
	Segment   gs { range_at(STATE_OFF + 0x50) };
	Segment gdtr { range_at(STATE_OFF + 0x60) };
	Segment ldtr { range_at(STATE_OFF + 0x70) };
	Segment idtr { range_at(STATE_OFF + 0x80) };
	Segment   tr { range_at(STATE_OFF + 0x90) };

	struct Efer : Register<STATE_OFF + 0xd0, 64>
	{
		struct Svm : Bitfield<12,1> { };
	};

	struct Cr4            : Register<STATE_OFF + 0x148, 64> { };
	struct Cr3            : Register<STATE_OFF + 0x150, 64> { };
	struct Cr0            : Register<STATE_OFF + 0x158, 64> { };
	struct Dr7            : Register<STATE_OFF + 0x160, 64> { };
	struct Rflags         : Register<STATE_OFF + 0x170, 64> { };
	struct Rip            : Register<STATE_OFF + 0x178, 64> { };
	struct Rsp            : Register<STATE_OFF + 0x1d8, 64> { };
	struct Rax            : Register<STATE_OFF + 0x1f8, 64> { };
	struct Star           : Register<STATE_OFF + 0x200, 64> { };
	struct Lstar          : Register<STATE_OFF + 0x208, 64> { };
	struct Cstar          : Register<STATE_OFF + 0x210, 64> { };
	struct Sfmask         : Register<STATE_OFF + 0x218, 64> { };
	struct Kernel_gs_base : Register<STATE_OFF + 0x220, 64> { };
	struct Sysenter_cs    : Register<STATE_OFF + 0x228, 64> { };
	struct Sysenter_esp   : Register<STATE_OFF + 0x230, 64> { };
	struct Sysenter_eip   : Register<STATE_OFF + 0x238, 64> { };
	struct Cr2            : Register<STATE_OFF + 0x240, 64> { };
	struct G_pat          : Register<STATE_OFF + 0x268, 64> { };

	Vmcb_buf(addr_t vmcb_addr, uint32_t id);
};



/*
 * VMCB data structure
 * See: AMD Manual Vol. 2, Appendix B Layout of VMCB
 */
struct Board::Vmcb
:
	public Board::Virt_interface
{
	static constexpr uint32_t ASID_HOST = 0;

	Vmcb_buf v;
	addr_t root_vmcb_phys = { 0 };

	Vmcb(Board::Vcpu_state &state, uint32_t id);
	static Vmcb_buf &host_vmcb(size_t cpu_id);
	void enforce_intercepts(uint32_t desired_primary   = 0U,
	                        uint32_t desired_secondary = 0U);
	void initialize(Board::Cpu &cpu,
	                addr_t page_table_phys_addr) override;
	void load(Genode::Vcpu_state &state) override;
	void store(Genode::Vcpu_state &state) override;
	void switch_world(Cpu_state &, addr_t) override;
	uint64_t handle_vm_exit() override;

	Virt_type virt_type() override
	{
		return Virt_type::SVM;
	}
};

static_assert(sizeof(Board::Vmcb) <= Genode::PAGE_SIZE);

#endif /* _INCLUDE__SPEC__PC__SVM_H_ */
