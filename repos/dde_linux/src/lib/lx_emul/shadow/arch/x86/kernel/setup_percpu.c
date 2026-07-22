/*
 * \brief  Replaces arch/x86/kernel/setup_percpu.c
 * \author Stefan Kalkowski
 * \date   2022-07-20
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <linux/version.h>
#include <asm/processor.h>
#include <asm/cpu.h>

DEFINE_PER_CPU_CACHE_HOT(unsigned long, this_cpu_off) = 0;
EXPORT_PER_CPU_SYMBOL(this_cpu_off);

int cpu_number = 0;

unsigned long __per_cpu_offset[NR_CPUS] = { 0UL };
