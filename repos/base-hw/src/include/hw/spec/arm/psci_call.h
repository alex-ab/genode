/**
 * \brief  PSCI call macro for arm_v7
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

#define PSCI_CALL_IMPL(instr) \
	static inline void call(Genode::umword_t  a0, Genode::umword_t  a1, \
	                        Genode::umword_t  a2, Genode::umword_t  a3, \
	                        Genode::umword_t  a4, Genode::umword_t  a5, \
	                        Genode::umword_t  a6, Genode::umword_t  a7, \
	                        Genode::umword_t &r0, Genode::umword_t &r1, \
	                        Genode::umword_t &r2, Genode::umword_t &r3) { \
		asm volatile ("mov r0, %4 \n" \
		              "mov r1, %5 \n" \
		              "mov r2, %6 \n" \
		              "mov r3, %7 \n" \
		              "mov r4, %8 \n" \
		              "mov r5, %9 \n" \
		              "mov r6, %10 \n" \
		              "mov r7, %11 \n" \
		              #instr " #0 \n" \
		              "mov %0, r0 \n" \
		              "mov %1, r1 \n" \
		              "mov %2, r2 \n" \
		              "mov %3, r3 \n" \
		               : "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3) \
		               : "r"(a0), "r"(a1), "r"(a2), "r"(a3), \
		                 "r"(a4), "r"(a5), "r"(a6), "r"(a7) \
		               : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7"); \
	}

namespace Hw {
	struct Psci_hvc_functor { PSCI_CALL_IMPL(hvc); };
	struct Psci_smc_functor { PSCI_CALL_IMPL(smc); };
}

#undef PSCI_CALL_IMPL

#endif /* _SRC__INCLUDE__HW__SPEC__ARM__PSCI_CALL_H_ */
