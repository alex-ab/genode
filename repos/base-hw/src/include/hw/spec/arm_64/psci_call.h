/**
 * \brief  PSCI call macro for arm_v8
 * \author Piotr Tworek
 * \date   2019-11-27
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__INCLUDE__HW__SPEC__ARM__PSCI_CALL_H_
#define _SRC__INCLUDE__HW__SPEC__ARM__PSCI_CALL_H_

#include <hw/spec/arm/psci.h>

/* following SMCCC: passes registers x0-x7, returns x0-x3 */
#define PSCI_CALL_IMPL(instr) \
	static inline void call(Genode::umword_t  a0, Genode::umword_t  a1, \
	                        Genode::umword_t  a2, Genode::umword_t  a3, \
	                        Genode::umword_t  a4, Genode::umword_t  a5, \
	                        Genode::umword_t  a6, Genode::umword_t  a7, \
	                        Genode::umword_t &r0, Genode::umword_t &r1, \
	                        Genode::umword_t &r2, Genode::umword_t &r3) { \
		asm volatile ("mov x0, %4 \n" \
		              "mov x1, %5 \n" \
		              "mov x2, %6 \n" \
		              "mov x3, %7 \n" \
		              "mov x4, %8 \n" \
		              "mov x5, %9 \n" \
		              "mov x6, %10 \n" \
		              "mov x7, %11 \n" \
		              #instr " #0 \n" \
		              "mov %0, x0 \n" \
		              "mov %1, x1 \n" \
		              "mov %2, x2 \n" \
		              "mov %3, x3 \n" \
		               : "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3) \
		               : "r"(a0), "r"(a1), "r"(a2), "r"(a3), \
		                 "r"(a4), "r"(a5), "r"(a6), "r"(a7) \
		               : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", \
		                 "x8", "x9", "x10", "x11", "x12", "x13", "x14", \
		                 "x15", "x16", "x17"); \
	}

namespace Hw {
	struct Psci_hvc_functor { PSCI_CALL_IMPL(hvc); };
	struct Psci_smc_functor { PSCI_CALL_IMPL(smc); };
}

#undef PSCI_CALL_IMPL

#endif /* _SRC__INCLUDE__HW__SPEC__ARM__PSCI_CALL_H_ */
