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
	inline void reply(void *next_sp, unsigned mtd, unsigned long sm = 0)
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
