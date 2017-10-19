/*
 * \brief   Nit6 SOLOX specific board definitions
 * \author  Stefan Kalkowski
 * \date    2017-10-18
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__BOOTSTRAP__SPEC__NIT6_SOLOX__BOARD_H_
#define _SRC__BOOTSTRAP__SPEC__NIT6_SOLOX__BOARD_H_

#include <drivers/defs/nit6_solox.h>
#include <drivers/uart/imx.h>
#include <hw/spec/arm/cortex_a9.h>
#include <hw/spec/arm/pl310.h>

#include <spec/arm/cortex_a9_actlr.h>
#include <spec/arm/cortex_a9_page_table.h>
#include <spec/arm/cpu.h>
#include <spec/arm/pic.h>

namespace Board {

	using namespace Nit6_solox;

	struct L2_cache;
	using Cpu_mmio = Hw::Cortex_a9_mmio<CORTEX_A9_PRIVATE_MEM_BASE>;
	using Serial   = Genode::Imx_uart;

	enum {
		UART_BASE  = UART_1_MMIO_BASE,
		UART_SIZE  = UART_1_MMIO_SIZE,
		UART_CLOCK = 0, /* dummy value, not used */
	};

	static volatile unsigned long initial_values[][2] {

		// (IOMUX Controller)
		{ 0x20E006C, 0x0},
		{ 0x20E00CC, 0x10},
		{ 0x20E00D0, 0x10},
		{ 0x20E00D4, 0x10},
		{ 0x20E00D8, 0x10},
		{ 0x20E00DC, 0x10},
		{ 0x20E00E0, 0x10},
		{ 0x20E00E4, 0x10},
		{ 0x20E00E8, 0x10},
		{ 0x20E00EC, 0x10},
		{ 0x20E00F0, 0x10},
		{ 0x20E00F4, 0x10},
		{ 0x20E00F8, 0x10},
		{ 0x20E00FC, 0x10},
		{ 0x20E0100, 0x10},
		{ 0x20E0104, 0x10},
		{ 0x20E0108, 0x10},
		{ 0x20E010C, 0x10},
		{ 0x20E0110, 0x10},
		{ 0x20E0114, 0x10},
		{ 0x20E0118, 0x10},
		{ 0x20E011C, 0x10},
		{ 0x20E0120, 0x10},
		{ 0x20E0124, 0x10},
		{ 0x20E0128, 0x10},
		{ 0x20E012C, 0x10},
		{ 0x20E0130, 0x10},
		{ 0x20E0134, 0x10},
		{ 0x20E0138, 0x15},
		{ 0x20E013C, 0x10},
		{ 0x20E0150, 0x5},
		{ 0x20E0154, 0x5},
		{ 0x20E0158, 0x5},
		{ 0x20E0224, 0x5},
		{ 0x20E0268, 0x5},
		{ 0x20E026C, 0x5},
		{ 0x20E0270, 0x3},
		{ 0x20E0274, 0x3},
		{ 0x20E035C, 0x1b8b1},
		{ 0x20E0360, 0x1b8b1},
		{ 0x20E0364, 0x1b8b1},
		{ 0x20E0368, 0x1b8b1},
		{ 0x20E0380, 0x1b0b0},
		{ 0x20E0384, 0x170b1},
		{ 0x20E0390, 0x110b0},
		{ 0x20E0394, 0x110b0},
		{ 0x20E0398, 0x110b0},
		{ 0x20E039C, 0x110b0},
		{ 0x20E03A0, 0x110b0},
		{ 0x20E03A4, 0x110b0},
		{ 0x20E03A8, 0x110b0},
		{ 0x20E03AC, 0x110b0},
		{ 0x20E03B0, 0x110b0},
		{ 0x20E03B4, 0x110b0},
		{ 0x20E03B8, 0x110b0},
		{ 0x20E03BC, 0x110b0},
		{ 0x20E03C0, 0x110b0},
		{ 0x20E03D4, 0xb0b0},
		{ 0x20E03D8, 0xb0b0},
		{ 0x20E03DC, 0xb0b0},
		{ 0x20E03E0, 0xb0b0},
		{ 0x20E03E4, 0xb0b0},
		{ 0x20E03E8, 0xb0b0},
		{ 0x20E03FC, 0x1b8b1},
		{ 0x20E0404, 0xb0b1},
		{ 0x20E0408, 0x1b0b0},
		{ 0x20E0410, 0x1b8b1},
		{ 0x20E0414, 0x1b0b0},
		{ 0x20E0418, 0x1b0b0},
		{ 0x20E041C, 0x1b0b0},
		{ 0x20E0420, 0x1b0b0},
		{ 0x20E0424, 0x1b0b0},
		{ 0x20E0428, 0x1b0b0},
		{ 0x20E042C, 0x1b0b0},
		{ 0x20E0430, 0x1b0b0},
		{ 0x20E0434, 0x1b0b0},
		{ 0x20E0438, 0x1b0b0},
		{ 0x20E043C, 0x1b0b0},
		{ 0x20E0440, 0x1b0b0},
		{ 0x20E0444, 0x1b0b0},
		{ 0x20E0448, 0x1b0b0},
		{ 0x20E044C, 0x1b0b0},
		{ 0x20E0450, 0x1b0b0},
		{ 0x20E0454, 0x1b0b0},
		{ 0x20E0458, 0x1b0b0},
		{ 0x20E045C, 0x1b0b0},
		{ 0x20E0460, 0x1b0b0},
		{ 0x20E0464, 0x1b0b0},
		{ 0x20E0468, 0x1b0b0},
		{ 0x20E046C, 0x1b0b0},
		{ 0x20E0470, 0x1b0b0},
		{ 0x20E0474, 0x1b0b0},
		{ 0x20E0478, 0x1b0b0},
		{ 0x20E047C, 0x1b0b0},
		{ 0x20E0480, 0x1b0b0},
		{ 0x20E0484, 0x1b0b0},
		{ 0x20E0488, 0xb0b0},
		{ 0x20E0490, 0x30b0},
		{ 0x20E0498, 0x30b1},
		{ 0x20E049C, 0x30b0},
		{ 0x20E04A0, 0x30b1},
		{ 0x20E04A4, 0xb0b1},
		{ 0x20E04AC, 0x30b0},
		{ 0x20E04B0, 0x30b0},
		{ 0x20E04B4, 0x30b0},
		{ 0x20E04E0, 0xb0b0},
		{ 0x20E04E8, 0xb0b0},
		{ 0x20E04F0, 0xb0b0},
		{ 0x20E04F4, 0xb0b0},
		{ 0x20E0508, 0x3081},
		{ 0x20E050C, 0x3081},
		{ 0x20E0510, 0x3081},
		{ 0x20E0514, 0x3081},
		{ 0x20E0518, 0x3081},
		{ 0x20E051C, 0x3081},
		{ 0x20E0520, 0x30b1},
		{ 0x20E0524, 0x30b1},
		{ 0x20E0528, 0x30b1},
		{ 0x20E052C, 0x30b1},
		{ 0x20E0530, 0x30b1},
		{ 0x20E0534, 0x30b1},
		{ 0x20E0538, 0x3081},
		{ 0x20E053C, 0x3081},
		{ 0x20E0540, 0x3081},
		{ 0x20E0544, 0x3081},
		{ 0x20E0548, 0x3081},
		{ 0x20E054C, 0x3081},
		{ 0x20E0550, 0x30b1},
		{ 0x20E0554, 0x30b1},
		{ 0x20E0558, 0x30b1},
		{ 0x20E055C, 0x30b1},
		{ 0x20E0560, 0x30b1},
		{ 0x20E0564, 0x30b1},
		{ 0x20E056C, 0x30b0},
		{ 0x20E0570, 0x1b0b0},
		{ 0x20E0574, 0x1b0b0},
		{ 0x20E0578, 0x1b0b0},
		{ 0x20E057C, 0x1b0b0},
		{ 0x20E0598, 0x10071},
		{ 0x20E059C, 0x17071},
		{ 0x20E05A0, 0x17071},
		{ 0x20E05A4, 0x17071},
		{ 0x20E05A8, 0x17071},
		{ 0x20E05AC, 0x17071},
		{ 0x20E05B0, 0x1b0b0},
		{ 0x20E05B4, 0x1b0b0},
		{ 0x20E05B8, 0x1b0b1},
		{ 0x20E05BC, 0x1b0b1},
		{ 0x20E05C0, 0x100f9},
		{ 0x20E05C4, 0x170f9},
		{ 0x20E05C8, 0x170f9},
		{ 0x20E05CC, 0x170f9},
		{ 0x20E05D0, 0x170f9},
		{ 0x20E05D4, 0x170f9},
		{ 0x20E05D8, 0x170f9},
		{ 0x20E05DC, 0x170f9},
		{ 0x20E05E0, 0x170f9},
		{ 0x20E05E4, 0x170f9},
		{ 0x20E05E8, 0x17071},
		{ 0x20E083C, 0x2},
		// (Global Power Controller}
		{ 0x20DC000, 0x140000},
		{ 0x20DC008, 0x2077fe0b},
		{ 0x20DC00C, 0xff7db18f},
		{ 0x20DC010, 0xfbfe0003},
		{ 0x20DC014, 0xff2ff93f},
		// (Power Management Unit}
		{ 0x20C8120, 0x11775},
		{ 0x20C8140, 0x4c0016},
		{ 0x20C8160, 0x8003000a},
		// (Clock Controller Module}
		{ 0x20C4004, 0x20000},
		{ 0x20C4018, 0x269114},
		{ 0x20C401C, 0x4510a9c0},
		{ 0x20C4020, 0x13212c06},
		{ 0x20C4028, 0x0},
		{ 0x20C402C, 0x4b600},
		{ 0x20C4030, 0x30074792},
		{ 0x20C4038, 0x12153},
		{ 0x20C4054, 0x78},
		{ 0x20C4060, 0x10e008e},
		{ 0x20C4064, 0x2fe62},
		{ 0x20C4068, 0xf0c03f0f},
		{ 0x20C406C, 0x333c0c00},
		{ 0x20C4070, 0x3fff003f},
		{ 0x20C4074, 0xfff33ff3},
		{ 0x20C4078, 0xc0c3fc},
		{ 0x20C407C, 0xf030fff},
		{ 0x20C4080, 0x3cfc33},
		{ 0x20C8000, 0x80002053},
		{ 0x20C8010, 0x80003040},
		{ 0x20C8020, 0x3840},
		{ 0x20C8070, 0x119006},
		{ 0x20C80A0, 0x80002025},
		{ 0x20C80B0, 0x13a74},
		{ 0x20C80C0, 0xf4240},
		{ 0x20C80E0, 0x8030200f},
		{ 0x20C80F0, 0xd3d1d0cc},
		{ 0x20C8100, 0x5258d0db}
	};
}


struct Board::L2_cache : Hw::Pl310
{
	L2_cache(Genode::addr_t mmio) : Hw::Pl310(mmio)
	{
		Aux::access_t aux = 0;
		Aux::Full_line_of_zero::set(aux, true);
		Aux::Associativity::set(aux, Aux::Associativity::WAY_16);
		Aux::Way_size::set(aux, Aux::Way_size::KB_16);
		Aux::Replacement_policy::set(aux, Aux::Replacement_policy::PRAND);
		Aux::Ns_lockdown::set(aux, true);
		Aux::Data_prefetch::set(aux, true);
		Aux::Inst_prefetch::set(aux, true);
		Aux::Early_bresp::set(aux, true);
		write<Aux>(aux);

		Tag_ram::access_t tag_ram = 0;
		Tag_ram::Setup_latency::set(tag_ram, 2);
		Tag_ram::Read_latency::set(tag_ram, 3);
		Tag_ram::Write_latency::set(tag_ram, 1);
		write<Tag_ram>(tag_ram);

		Data_ram::access_t data_ram = 0;
		Data_ram::Setup_latency::set(data_ram, 2);
		Data_ram::Read_latency::set(data_ram, 3);
		Data_ram::Write_latency::set(data_ram, 1);
		write<Data_ram>(data_ram);

		Prefetch_ctrl::access_t prefetch = read<Prefetch_ctrl>();
		Prefetch_ctrl::Data_prefetch::set(prefetch, 1);
		Prefetch_ctrl::Inst_prefetch::set(prefetch, 1);
		write<Prefetch_ctrl>(prefetch | 0xF);
	}

	using Hw::Pl310::invalidate;

	void enable()
	{
		Pl310::mask_interrupts();
		write<Control::Enable>(1);
	}

	void disable() {
		write<Control::Enable>(0);
	}
};

#endif /* _SRC__BOOTSTRAP__SPEC__NIT6_SOLOX__BOARD_H_ */
