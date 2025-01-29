/*
 * \brief  Syscall bindings for the NOVAe microhypervisor x86_64
 * \author Alexander Boettcher
 * \date   2024-03-15
 */

/*
 * Copyright (c) 2025 Genode Labs
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

//#include <novae/stdint.h>
#include <novae/syscall-generic.h>

/* XXX tmp */
#include <base/log.h>

#define ALWAYS_INLINE __attribute__((always_inline))

namespace Novae {


	/**
	 * Event-specific capability selectors
	 */
	enum {
		PT_SEL_PAGE_FAULT = 0x24,  /* aarch64 architectural */
		PT_SEL_STARTUP    = 0x40,  /* defined by NOVA spec */
		PT_SEL_RECALL     = 0x41,  /* defined by NOVA spec */
		                           /* defined by NOVA spec */
		PT_SEL_DELEGATE   = 0x43,  /* convention on Genode */
		SM_SEL_EC         = 0x44,  /* convention on Genode */
		PT_SEL_PARENT     = 0x45,  /* convention on Genode */
		EC_SEL_THREAD     = 0x46,  /* convention on Genode */
		SM_SEL_SIGNAL     = 0x47,  /* convention on Genode */
	};


	/**
	 * Message-transfer descriptor
	 */
	class Mtd
	{
		private:

			mword_t const _value;

		public:

			enum Mtd_values {
				GPR            = 1U <<  2,
				EL0_SP         = 1U <<  4,
				EL2_ELR_SPSR   = 1U << 25,
				EL2_ESR_FAR    = 1U << 26,
			};

			static constexpr auto gpr_ip() {
				return Mtd::GPR | Mtd::EL2_ELR_SPSR; }

			static constexpr auto flags_status() {
				return Mtd::EL2_ELR_SPSR | Mtd::EL2_ESR_FAR; }

			static constexpr auto qual() { return Mtd::EL2_ESR_FAR; }
			static constexpr auto sp()   { return Mtd::EL0_SP; }

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

		auto sp (auto const value) { mr[0x0f8 / 8] = value; }
		auto ip (auto const value) { mr[0x1e0 / 8] = value; }

		auto sp () const { return mr[0x0f8 / 8]; }
		auto ip () const { return mr[0x1e0 / 8]; }

		auto pf_addr() const { return mr[0x1f8 / 8]; }
		auto pf_type() const { return mr[0x1f0 / 8]; }
	};

	static_assert(sizeof(Utcb) == 4096, "Unexpected size of UTCB");


	ALWAYS_INLINE
	inline mword_t x0(Syscall s, uint8_t flags, mword_t sel)
	{
		return sel << 8 | (flags & 0xf) << 4 | s;
	}


	ALWAYS_INLINE inline
	uint8_t syscall_1(Syscall const sys, uint8_t const flags,
	                  mword_t const sel, mword_t const p1)
	{
		register mword_t status asm ("x0") = x0(sys, flags, sel);
		register mword_t r_p1   asm ("x1") = p1;

		asm volatile ("svc 0" : "+r" (status) : "r" (r_p1) : "memory");

		return uint8_t(status);
	}


	ALWAYS_INLINE inline
	uint8_t syscall_1x(Syscall const sys, uint8_t const flags,
	                   mword_t const sel, mword_t & p1)
	{
		register mword_t status asm ("x0") = x0(sys, flags, sel);
		register mword_t r_p1   asm ("x1") = p1;

		asm volatile ("svc 0" : "+r" (status), "+r" (r_p1) : : "memory");

		p1 = r_p1;

		return uint8_t(status);
	}


	ALWAYS_INLINE inline
	uint8_t syscall_2(Syscall const sys, uint8_t const flags, mword_t const sel,
	                  mword_t const  p1, mword_t const p2)
	{
		register mword_t status asm ("x0") = x0(sys, flags, sel);
		register mword_t r_p1   asm ("x1") = p1;
		register mword_t r_p2   asm ("x2") = p2;

		asm volatile ("svc 0" : "+r" (status)
		                      : "r" (r_p1), "r" (r_p2) : "memory");

		return uint8_t(status);
	}


	ALWAYS_INLINE inline
	uint8_t syscall_3(Syscall const   s, uint8_t const flags,
	                  mword_t const sel, mword_t const p1,
	                  mword_t const  p2, mword_t const p3)
	{
		register mword_t status asm ("x0") = x0(s, flags, sel);
		register mword_t r_p1   asm ("x1") = p1;
		register mword_t r_p2   asm ("x2") = p2;
		register mword_t r_p3   asm ("x3") = p3;

		asm volatile ("svc 0"
		              : "+r" (status)
		              : "r" (r_p1), "r" (r_p2), "r" (r_p3)
		              : "memory");

		return uint8_t(status);
	}


	ALWAYS_INLINE inline
	uint8_t syscall_4(Syscall const sys, uint8_t const flags,
	                  mword_t const sel, mword_t const p1,
	                  mword_t const  p2, mword_t const p3, mword_t const p4)
	{
		register mword_t status asm ("x0") = x0(sys, flags, sel);
		register mword_t r_p1   asm ("x1") = p1;
		register mword_t r_p2   asm ("x2") = p2;
		register mword_t r_p3   asm ("x3") = p3;
		register mword_t r_p4   asm ("x4") = p4;

		asm volatile ("svc 0"
		              : "+r" (status)
		              : "r" (r_p1), "r" (r_p2), "r" (r_p3), "r" (r_p4)
		              : "memory");

		return uint8_t(status);
	}


#if 0
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
#endif

	ALWAYS_INLINE
	inline uint8_t call(mword_t pt, unsigned &mtd, uint8_t no_timeout = 0)
	{
		mword_t mtd_x  = mtd;
		uint8_t result = syscall_1x(NOVA_CALL, !!no_timeout, pt, mtd_x);
		mtd = mtd_x & ((1ul << 32) - 1);
		return result;
	}


	ALWAYS_INLINE
	__attribute__((noreturn))
	inline void reply(mword_t next_sp, unsigned mtd, unsigned long sm = 0)
	{
		register mword_t status asm ("x0") = x0(NOVA_REPLY, 0, sm);
		register mword_t r_p1   asm ("x1") = mtd;

		asm volatile ("mov sp, %x2;"
		              "svc 0" :
		                      : "r" (status), "r" (r_p1), "ir" (next_sp)
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
	ALWAYS_INLINE inline
	uint8_t create_ec(mword_t const   ec, mword_t const pd, mword_t const cpu,
	                  mword_t const utcb, mword_t const sp, mword_t const evt,
	                  bool    const global)
	{
		auto const flags = uint8_t((global ? 2u : 0u) | 4 /* FPU */);
		return syscall_4(NOVA_CREATE_EC, flags, ec, pd,
		                 utcb & ~0xffful, (evt << 16) | (cpu & 0xffffu), sp);
	}

	ALWAYS_INLINE
	inline uint8_t create_vcpu(mword_t ec, mword_t pd, mword_t cpu, mword_t vapic,
	                           mword_t sp, mword_t evt, bool time_offset)
	{
		(void)ec;
		(void)pd;
		(void)cpu;
		(void)vapic;
		(void)sp;
		(void)evt;
		(void)time_offset;
		Genode::error("Novae::", __func__, " not implemented");
		return -1;
#if 0
		auto flags = uint8_t(1 /* vCPU */ | (time_offset ? 2 : 0) | 4 /* FPU */);
		return syscall_4(NOVA_CREATE_EC, flags, ec, pd,
		                 (cpu & 0xfff) | (vapic & ~0xfff),
		                 sp, evt);
#endif
	}

	ALWAYS_INLINE inline
	uint8_t ec_ctrl(Ec_op const op, mword_t const ec = ~0UL,
	                mword_t const para = ~0UL, Crd const crd = 0)
	{
		return syscall_2(NOVA_EC_CTRL, op, ec, para, crd.value());
	}


	ALWAYS_INLINE inline
	uint8_t create_sc(mword_t const sc, mword_t const pd, mword_t const ec,
	                  Qpd const qpd)
	{
		return syscall_3(NOVA_CREATE_SC, 0, sc, pd, ec, qpd.value());
	}

	ALWAYS_INLINE inline
	uint8_t pt_ctrl(mword_t const pt, mword_t const pt_id, mword_t const mtd) {
		return syscall_2(NOVA_PT_CTRL, 0, pt, pt_id, mtd); }

	ALWAYS_INLINE inline
	uint8_t create_pt(mword_t const pt, mword_t const pd, mword_t const ec,
	                  mword_t const ip) {
		return syscall_3(NOVA_CREATE_PT, 0, pt, pd, ec, ip); }

	ALWAYS_INLINE inline
	uint8_t create_sm(mword_t const sm, mword_t const pd, mword_t const cnt) {
		return syscall_3(NOVA_CREATE_SM, 0, sm, pd, cnt, 0); }

	ALWAYS_INLINE inline
	uint8_t create_sm_irq(mword_t const sm, mword_t const pd,
	                      mword_t const cnt) {
		return syscall_3(NOVA_CREATE_SM, 1, sm, pd, cnt, 0); }

	ALWAYS_INLINE inline
	uint8_t sm_ctrl(mword_t const sm, Sem_op const op,
	                mword_t const timeout = 0) {
		return syscall_1(NOVA_SM_CTRL, op, sm, timeout); }

	ALWAYS_INLINE
	inline uint8_t sc_ctrl(mword_t const sc, unsigned long long &time)
	{
		(void)sc;
		(void)time;
		Genode::error("Novae::", __func__, " not implemented");
		return -1;
#if 0
		mword_t time_tmp = 0;
		auto res = syscall_1(NOVA_SC_CTRL, 0, sc, 0, &time_tmp);
		time = time_tmp;
		return res;
#endif
	}

	ALWAYS_INLINE inline
	uint8_t pd_ctrl(mword_t const pd_src,  mword_t const pd_dst,
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
		(void)sm;
		(void)flags;
		(void)cpu;
		(void)irq_idx;
		(void)sbdf;
		(void)msi_addr;
		(void)msi_data;
		Genode::error("Novae::", __func__, " not implemented");
		return -1;
#if 0
		msi_addr = ((cpu     & 0xfffful) <<  0) |
		           ((irq_idx & 0xfffful) << 16) |
		                           (sbdf << 32);
		msi_data = 0;
		return syscall_5(NOVA_ASSIGN_INT, flags, sm, msi_addr, msi_data);
#endif
	}
}
