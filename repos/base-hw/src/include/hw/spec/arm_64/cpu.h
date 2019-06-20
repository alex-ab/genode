/*
 * \brief  CPU definitions for ARM 64bit
 * \author Stefan Kalkowski
 * \date   2019-05-22
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__LIB__HW__SPEC__ARM_64__CPU_H_
#define _SRC__LIB__HW__SPEC__ARM_64__CPU_H_

#define SYSTEM_REGISTER(sz, name, reg, ...) \
	struct name : Genode::Register<sz> \
	{ \
		static access_t read() \
		{ \
			access_t v; \
			asm volatile ("mrs %0, " #reg : "=r" (v)); \
			return v; \
		} \
 \
		static void write(access_t const v) { \
			asm volatile ("msr " #reg ", %0" :: "r" (v)); } \
 \
		__VA_ARGS__; \
	};


namespace Hw { struct Arm_64_cpu; }

struct Hw::Arm_64_cpu
{
	SYSTEM_REGISTER(64, Id_pfr0, id_aa64pfr0_el1,
		struct El2 : Bitfield<8, 4> {};
		struct El3 : Bitfield<8, 4> {};
	);

	SYSTEM_REGISTER(64, Current_el, currentel,
		enum Level { EL0, EL1, EL2, EL3 };
		struct El : Bitfield<2, 2> {};
	);

	struct Esr : Genode::Register<64>
	{
		struct Ec : Bitfield<26, 6>
		{
			enum Exception {
				SVC                   = 0b010101,
				INST_ABORT_LOW_LEVEL  = 0b100000,
				INST_ABORT_SAME_LEVEL = 0b100001,
				DATA_ABORT_LOW_LEVEL  = 0b100100,
				DATA_ABORT_SAME_LEVEL = 0b100101,
			};
		};

		struct Iss : Bitfield<0, 25>
		{
			struct Abort : Register<32>
			{
				struct Level : Bitfield<0, 2> {};
				struct Fsc   : Bitfield<2, 4>
				{
					enum Fault {
						ADDR_SIZE,
						TRANSLATION,
						ACCESS_FLAG,
						PERMISSION
					};
					enum Data_abort_fault { ALIGNMENT = 8 };
				};
				struct Write : Bitfield<6, 1> {};
			};
		};
	};

	SYSTEM_REGISTER(64, Esr_el1, esr_el1);
	SYSTEM_REGISTER(64, Far_el1, far_el1);

	SYSTEM_REGISTER(64, Hcr_el2, hcr_el2,
		struct Fmo  : Bitfield< 3, 1> {};
		struct Imo  : Bitfield< 4, 1> {};
		struct Amo  : Bitfield< 5, 1> {};

		struct Tid0  : Bitfield<15, 1> {};
		struct Tid1  : Bitfield<16, 1> {};
		struct Tid2  : Bitfield<17, 1> {};
		struct Tid3  : Bitfield<18, 1> {};
		struct Tidcp : Bitfield<20, 1> {};

		struct Rw   : Bitfield<31, 1> {};
		struct Tlor : Bitfield<35, 1> {};
	);

	SYSTEM_REGISTER(64, Mair, mair_el1,
		enum Attributes {
			DEVICE_MEMORY          = 0x04,
			NORMAL_MEMORY_UNCACHED = 0x44,
			NORMAL_MEMORY_CACHED   = 0xff,
		};
		struct Attr0 : Bitfield<0,  8> {};
		struct Attr1 : Bitfield<8,  8> {};
		struct Attr2 : Bitfield<16, 8> {};
		struct Attr3 : Bitfield<24, 8> {};
	);

	SYSTEM_REGISTER(32, Pmuserenr_el0, pmuserenr_el0);

	SYSTEM_REGISTER(64, Scr, scr_el3,
		struct Ns  : Bitfield<0,  1> {};
		struct Smd : Bitfield<7,  1> {};
		struct Rw  : Bitfield<10, 1> {};
	);

	SYSTEM_REGISTER(64, Sctlr_el1, sctlr_el1,
		struct M   : Bitfield<0,  1> { };
		struct A   : Bitfield<1,  1> { };
		struct C   : Bitfield<2,  1> { };
		struct Sa  : Bitfield<3,  1> { };
		struct Sa0 : Bitfield<4,  1> { };
		struct I   : Bitfield<12, 1> { };
	);

	struct Spsr : Genode::Register<64>
	{
		struct Sp : Bitfield<0, 1> {};
		struct El : Bitfield<2, 2> {};
		struct F  : Bitfield<6, 1> {};
		struct I  : Bitfield<7, 1> {};
		struct A  : Bitfield<8, 1> {};
		struct D  : Bitfield<9, 1> {};
	};

	SYSTEM_REGISTER(64, Spsr_el2, spsr_el2);
	SYSTEM_REGISTER(64, Spsr_el3, spsr_el3);

	/* Virtualization Translation Control Register */
	SYSTEM_REGISTER(32, Vtcr_el2, vtcr_el2,
		struct T0sz : Bitfield<0, 6> {};
		struct Sl0  : Bitfield<6, 2> {}; /* starting level for table walks */
	);

	SYSTEM_REGISTER(64, Tcr_el1, tcr_el1,
		struct T0sz  : Bitfield<0,  6> { };
		struct Epd0  : Bitfield<7,  1> { };
		struct Irgn0 : Bitfield<8,  2> { };
		struct Orgn0 : Bitfield<10, 2> { };
		struct Sh0   : Bitfield<12, 2> { };
		struct Tg0   : Bitfield<14, 2> { };
		struct T1sz  : Bitfield<16, 6> { };
		struct A1    : Bitfield<22, 1> { };
		struct Epd1  : Bitfield<23, 1> { };
		struct Irgn1 : Bitfield<24, 2> { };
		struct Orgn1 : Bitfield<26, 2> { };
		struct Sh1   : Bitfield<28, 2> { };
		struct Tg1   : Bitfield<30, 2> { };
		struct Ips   : Bitfield<32, 3> { };
		struct As    : Bitfield<36, 1> { };
	);

	struct Ttbr : Genode::Register<64>
	{
		struct Baddr : Bitfield<0,  48> { };
		struct Asid  : Bitfield<48, 16> { };
	};

	SYSTEM_REGISTER(64, Ttbr0_el1, ttbr0_el1);
	SYSTEM_REGISTER(64, Ttbr1_el1, ttbr1_el1);

	SYSTEM_REGISTER(64, Vbar_el1, vbar_el1);

	SYSTEM_REGISTER(64, Elr_el1, elr_el1);
	SYSTEM_REGISTER(64, Sp_el0, sp_el0);
	SYSTEM_REGISTER(32, Spsel, spsel);
	SYSTEM_REGISTER(32, Spsr_el1, spsr_el1);
	SYSTEM_REGISTER(32, Daif, daif);
	SYSTEM_REGISTER(32, Nzcv, nzcv);
	SYSTEM_REGISTER(32, Fpcr, fpcr);
	SYSTEM_REGISTER(32, Fpsr, fpsr);

	SYSTEM_REGISTER(64, Vbar_el2, vbar_el2);
	SYSTEM_REGISTER(64, Ttbr0_el2, ttbr0_el2);

	SYSTEM_REGISTER(64, Tcr_el2, tcr_el2,
		struct T0sz  : Bitfield<0,  6> { };
		struct Irgn0 : Bitfield<8,  2> { };
		struct Orgn0 : Bitfield<10, 2> { };
		struct Sh0   : Bitfield<12, 2> { };
	);

	SYSTEM_REGISTER(64, Sctlr_el2, sctlr_el2,
		struct M   : Bitfield< 0, 1> { };
		struct A   : Bitfield< 1, 1> { };
		struct C   : Bitfield< 2, 1> { };
		struct Sa  : Bitfield< 3, 1> { };
		struct I   : Bitfield<12, 1> { };
		struct Wxn : Bitfield<19, 1> { };
	);

	SYSTEM_REGISTER(64, Vttbr_el2, vttbr_el2,
		struct CnP  : Bitfield<0,  1>  { };
		struct Ba   : Bitfield<1, 47>  { }; /* translation table base */
		struct Asid : Bitfield<48, 8>  { }; /* XXX implementation defined whether 8 or 16 bit in use ... */
	);

	SYSTEM_REGISTER(64, Mair_el2, mair_el2,
		enum Attributes {
			DEVICE_MEMORY          = 0x04,
			NORMAL_MEMORY_UNCACHED = 0x44,
			NORMAL_MEMORY_CACHED   = 0xff,
		};
		struct Attr0 : Bitfield<0,  8> {};
		struct Attr1 : Bitfield<8,  8> {};
		struct Attr2 : Bitfield<16, 8> {};
		struct Attr3 : Bitfield<24, 8> {};
	);

	SYSTEM_REGISTER(32, Cpacr_el1, cpacr_el1);
	SYSTEM_REGISTER(32, Afsr0_el1, afsr0_el1);
	SYSTEM_REGISTER(32, Afsr1_el1, afsr1_el1);
	SYSTEM_REGISTER(64, Actlr_el1, actlr_el1);
	SYSTEM_REGISTER(64, Amair_el1, amair_el1);
	SYSTEM_REGISTER(32, Contextidr_el1, contextidr_el1);
	SYSTEM_REGISTER(32, Csselr_el1, csselr_el1);

	SYSTEM_REGISTER(64, Par_el1, par_el1);
	SYSTEM_REGISTER(64, Tpidrro_el0, tpidrro_el0);
	SYSTEM_REGISTER(64, Tpidr_el0, tpidr_el0);
	SYSTEM_REGISTER(64, Tpidr_el1, tpidr_el1);

	SYSTEM_REGISTER(32, Mdcr_el2, mdcr_el2,
		struct Tpm   : Bitfield< 5, 1> {};
		struct Tpmcr : Bitfield< 6, 1> {};
		struct Tda   : Bitfield< 9, 1> {};
		struct Tdosa : Bitfield<10, 1> {};
	);

	static inline unsigned current_privilege_level() {
		return Current_el::El::get(Current_el::read()); }


	/*****************************
	 ** Generic timer interface **
	 *****************************/

	SYSTEM_REGISTER(64, Cntfrq_el0, cntfrq_el0);

	SYSTEM_REGISTER(32, Cntp_ctl_el0, cntp_ctl_el0,
		struct Enable  : Bitfield<0, 1> {};
		struct Istatus : Bitfield<2, 1> {};
	);

	SYSTEM_REGISTER(64, Cntpct_el0, cntpct_el0);
	SYSTEM_REGISTER(32, Cntp_tval_el0, cntp_tval_el0);

	using Cntfrq    = Cntfrq_el0;
	using Cntp_ctl  = Cntp_ctl_el0;
	using Cntpct    = Cntpct_el0;
	using Cntp_tval = Cntp_tval_el0;
};


#undef SYSTEM_REGISTER

#endif /* _SRC__LIB__HW__SPEC__ARM_64__CPU_H_ */
