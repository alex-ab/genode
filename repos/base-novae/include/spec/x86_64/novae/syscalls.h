/*
 * \brief  Syscall bindings for the NOVAe microhypervisor x86_64
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \author Alexander Boettcher
 * \date   2012-06-06
 */

/*
 * Copyright (c) 2012-2025 Genode Labs
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <novae/stdint.h>
#include <novae/syscall-generic.h>

#define ALWAYS_INLINE __attribute__((always_inline))

namespace Novae {


	/**
	 * Event-specific capability selectors
	 */
	enum {
		PT_SEL_PAGE_FAULT = 0x0e,  /* x86 architectural */
		PT_SEL_STARTUP    = 0x20,  /* defined by NOVA spec */
		PT_SEL_RECALL     = 0x21,  /* defined by NOVA spec */
		PT_SEL_DELEGATE   = 0x22,  /* convention on Genode */
		SM_SEL_EC         = 0x23,  /* convention on Genode */
		PT_SEL_PARENT     = 0x24,  /* convention on Genode */
		EC_SEL_THREAD     = 0x25,  /* convention on Genode */
		SM_SEL_SIGNAL     = 0x26,  /* convention on Genode */
	};


	/**
	 * Message-transfer descriptor
	 */
	class Mtd
	{
		private:

			mword_t const _value;

		public:

			enum {
				GPR_0_7        = 1U << 1,
				GPR_8_15       = 1U << 2,
				RFLAGS         = 1U << 3,
				RIP            = 1U << 4,
				QUAL           = 1U << 6,  /* exit qualification */
			};

			static constexpr auto gpr_ip() {
				return Mtd::GPR_0_7 | Mtd::RIP; }

			static constexpr auto flags_status() { return Mtd::RFLAGS; }

			static constexpr auto sp()   { return Mtd::GPR_0_7; }

			static constexpr auto qual() { return Mtd::QUAL; }

			Mtd(mword_t value) : _value(value) { }

			mword_t value() const { return _value; }
	};


	/**
	 * User-level thread-control block
	 */
	struct Utcb
	{
		/**
		 * Return physical size of UTCB in bytes
		 */
		static constexpr mword_t size() { return 4096; }

		mword_t mr[512];

		mword_t * msg() { return mr; }

		auto ax (auto const value) { mr[0x00 / 8] = value; }
		auto cx (auto const value) { mr[0x08 / 8] = value; }
		auto dx (auto const value) { mr[0x10 / 8] = value; }
		auto bx (auto const value) { mr[0x18 / 8] = value; }
		auto sp (auto const value) { mr[0x20 / 8] = value; }
		auto bp (auto const value) { mr[0x28 / 8] = value; }
		auto si (auto const value) { mr[0x30 / 8] = value; }
		auto di (auto const value) { mr[0x38 / 8] = value; }
		auto r8 (auto const value) { mr[0x40 / 8] = value; }
		auto r9 (auto const value) { mr[0x48 / 8] = value; }
		auto r10(auto const value) { mr[0x50 / 8] = value; }
		auto r11(auto const value) { mr[0x58 / 8] = value; }
		auto r12(auto const value) { mr[0x60 / 8] = value; }
		auto r13(auto const value) { mr[0x68 / 8] = value; }
		auto r14(auto const value) { mr[0x70 / 8] = value; }
		auto r15(auto const value) { mr[0x78 / 8] = value; }
		auto fl (auto const value) { mr[0x80 / 8] = value; }
		auto ip (auto const value) { mr[0x88 / 8] = value; }

		auto ax () const { return mr[0x00 / 8]; }
		auto cx () const { return mr[0x08 / 8]; }
		auto dx () const { return mr[0x10 / 8]; }
		auto bx () const { return mr[0x18 / 8]; }
		auto sp () const { return mr[0x20 / 8]; }
		auto bp () const { return mr[0x28 / 8]; }
		auto si () const { return mr[0x30 / 8]; }
		auto di () const { return mr[0x38 / 8]; }
		auto r8 () const { return mr[0x40 / 8]; }
		auto r9 () const { return mr[0x48 / 8]; }
		auto r10() const { return mr[0x50 / 8]; }
		auto r11() const { return mr[0x58 / 8]; }
		auto r12() const { return mr[0x60 / 8]; }
		auto r13() const { return mr[0x68 / 8]; }
		auto r14() const { return mr[0x70 / 8]; }
		auto r15() const { return mr[0x78 / 8]; }
		auto fl () const { return mr[0x80 / 8]; }
		auto ip () const { return mr[0x88 / 8]; }

		auto    qual_1()  const { return         mr[0xa0 / 8]; }
		auto    pf_addr() const { return         mr[0xa8 / 8]; }
		uint8_t pf_type() const { return uint8_t(mr[0xa0 / 8]); }

	};

	static_assert(sizeof(Utcb) == 4096, "Unexpected size of UTCB");


	ALWAYS_INLINE
	inline mword_t rdi(Syscall s, uint8_t flags, mword_t sel)
	{
		return sel << 8 | (flags & 0xf) << 4 | s;
	}


	ALWAYS_INLINE
	inline uint8_t syscall_1(Syscall s, uint8_t flags, mword_t sel, mword_t p1,
	                         mword_t * p2 = 0)
	{
		mword_t status = rdi(s, flags, sel);

		asm volatile ("syscall"
		              : "+D" (status), "+S" (p1)
		              :
		              : "rcx", "r11", "memory");
		if (p2) *p2 = p1;
		return  (uint8_t)status;
	}


	ALWAYS_INLINE
	inline uint8_t syscall_2(Syscall s, uint8_t flags, mword_t sel, mword_t p1,
	                         mword_t p2)
	{
		mword_t status = rdi(s, flags, sel);

		asm volatile ("syscall"
		              : "+D" (status)
		              : "S" (p1), "d" (p2)
		              : "rcx", "r11", "memory");
		return  (uint8_t)status;
	}


	ALWAYS_INLINE
	inline uint8_t syscall_3(Syscall s, uint8_t flags, mword_t sel,
	                         mword_t p1, mword_t p2, mword_t p3)
	{
		mword_t status = rdi(s, flags, sel);

		asm volatile ("syscall"
		              : "+D" (status)
		              : "S" (p1), "d" (p2), "a" (p3)
		              : "rcx", "r11", "memory");
		return  (uint8_t)status;
	}


	ALWAYS_INLINE
	inline uint8_t syscall_4(Syscall s, uint8_t flags, mword_t sel,
	                         mword_t p1, mword_t p2, mword_t p3, mword_t p4)
	{
		mword_t status = rdi(s, flags, sel);
		register mword_t r8 asm ("r8") = p4;

		asm volatile ("syscall;"
		              : "+D" (status)
		              : "S" (p1), "d" (p2), "a" (p3), "r" (r8)
		              : "rcx", "r11", "memory");
		return  (uint8_t)status;
	}


	ALWAYS_INLINE
	inline uint8_t syscall_5(Syscall s, uint8_t flags, mword_t sel,
	                         mword_t &p1, mword_t &p2, mword_t p3 = ~0UL)
	{
		mword_t status = rdi(s, flags, sel);

		asm volatile ("syscall"
		              : "+D" (status), "+S"(p1), "+d"(p2)
		              : "a" (p3)
		              : "rcx", "r11", "memory");
		return  (uint8_t)status;
	}

	ALWAYS_INLINE
	inline uint8_t call(mword_t pt, unsigned &mtd, uint8_t no_timeout = 0)
	{
		mword_t status = rdi(NOVA_CALL, no_timeout, pt);

		asm volatile ("syscall"
		              : "+D" (status), "+S" (mtd)
		              :
		              : "rcx", "r11", "memory");

		return  (uint8_t)status;
	}


	ALWAYS_INLINE
	__attribute__((noreturn))
	inline void reply(mword_t next_sp, unsigned mtd, unsigned long sm = 0)
	{
		mword_t syscall = rdi(NOVA_REPLY, 0, sm);

		asm volatile ("mov %2, %%rsp;"
		              "syscall;"
		              :
		              : "D" (syscall), "S" (mtd), "ir" (next_sp)
		              : "memory");
		__builtin_unreachable();
	}


	ALWAYS_INLINE
	inline uint8_t create_pd(mword_t sel, mword_t pd, uint8_t flags)
	{
		return syscall_1(NOVA_CREATE_PD, flags, sel, pd);
	}


	/**
	 * Create an EC.
	 *
	 * \param ec     Unused selector to be used for new EC
	 * \param pd     Selector of PD the EC will created in
	 * \param cpu    CPU number the EC will run on
	 * \param utcb   PD local address where the UTCB of the EC will be appear
	 * \param esp    initial stack address
	 * \param evt    base selector for all exception portals of the EC
	 * \param global if true  - thread requires a SC to be runnable
	 *               if false - thread is runnable solely if it receives a IPC
	 *                          (worker thread)
	 */
	ALWAYS_INLINE
	inline uint8_t create_ec(mword_t ec, mword_t pd, mword_t cpu, mword_t utcb,
	                         mword_t sp, mword_t evt, bool global)
	{
		auto flags = uint8_t((global ? 2u : 0u) | 4 /* FPU */);
		return syscall_4(NOVA_CREATE_EC, flags, ec, pd,
		                 utcb & ~0xffful, (evt << 16) | (cpu & 0xffffu), sp);
	}

	ALWAYS_INLINE
	inline uint8_t create_vcpu(mword_t ec, mword_t pd, mword_t cpu, mword_t vapic,
	                           mword_t sp, mword_t evt, bool time_offset)
	{
		auto flags = uint8_t(1 /* vCPU */ | (time_offset ? 2 : 0) | 4 /* FPU */);
		return syscall_4(NOVA_CREATE_EC, flags, ec, pd,
		                 (cpu & 0xfff) | (vapic & ~0xfff),
		                 sp, evt);
	}

	ALWAYS_INLINE
	inline uint8_t ec_ctrl(Ec_op op, mword_t ec = ~0UL, mword_t para = ~0UL,
	                       Crd crd = 0)
	{
		return syscall_2(NOVA_EC_CTRL, op, ec, para, crd.value());
	}


	ALWAYS_INLINE
	inline uint8_t create_sc(mword_t sc, mword_t pd, mword_t ec, Qpd qpd)
	{
		return syscall_3(NOVA_CREATE_SC, 0, sc, pd, ec, qpd.value());
	}


	ALWAYS_INLINE
	inline uint8_t pt_ctrl(mword_t pt, mword_t pt_id, mword_t mtd)
	{
		return syscall_2(NOVA_PT_CTRL, 0, pt, pt_id, mtd);
	}


	ALWAYS_INLINE
	inline uint8_t create_pt(mword_t pt, mword_t pd, mword_t ec, mword_t ip)
	{
		return syscall_3(NOVA_CREATE_PT, 0, pt, pd, ec, ip);
	}


	ALWAYS_INLINE
	inline uint8_t create_sm(mword_t sm, mword_t pd, mword_t cnt)
	{
		return syscall_3(NOVA_CREATE_SM, 0, sm, pd, cnt, 0);
	}


	ALWAYS_INLINE
	inline uint8_t create_sm_irq(mword_t sm, mword_t pd, mword_t cnt)
	{
		return syscall_3(NOVA_CREATE_SM, 1, sm, pd, cnt, 0);
	}


	ALWAYS_INLINE
	inline uint8_t sm_ctrl(mword_t sm, Sem_op op, unsigned long long timeout = 0)
	{
		return syscall_1(NOVA_SM_CTRL, op, sm, timeout);
	}


	ALWAYS_INLINE
	inline uint8_t sc_ctrl(mword_t const sc, unsigned long long &time)
	{
		mword_t time_tmp = 0;
		auto res = syscall_1(NOVA_SC_CTRL, 0, sc, 0, &time_tmp);
		time = time_tmp;
		return res;
	}


	ALWAYS_INLINE
	inline uint8_t pd_ctrl(mword_t const pd_src,  mword_t const pd_dst,
	                       mword_t const ssb_ord, mword_t const dsb_pmm,
	                       mword_t const mad)
	{
		return syscall_4(NOVA_PD_CTRL, 0, pd_src, pd_dst, ssb_ord, dsb_pmm, mad);
	}


	ALWAYS_INLINE
	inline uint8_t assign_int(mword_t sm, uint8_t flags, mword_t cpu,
	                          mword_t irq_idx, mword_t sbdf, mword_t &msi_addr,
	                          mword_t &msi_data)
	{
		msi_addr = ((cpu     & 0xfffful) <<  0) |
		           ((irq_idx & 0xfffful) << 16) |
		                           (sbdf << 32);
		msi_data = 0;
		return syscall_5(NOVA_ASSIGN_INT, flags, sm, msi_addr, msi_data);
	}
}
