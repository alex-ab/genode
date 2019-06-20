/*
 * \brief  Transition between virtual/host mode
 * \author Alexander Boettcher
 * \date   2019-06-25
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

.macro _host_ptr
# XXX evil
	ldr x4, _vt_host_context_ptr
	and x4, x4, 0xfff

	mov x3, 0x7fffff0000
	add x3, x3, 0x4000

	add x3, x3, x4
# XXX evil
.endm

.macro _vm_exit exception_type
	str x1, [sp, #-0*8]
	str x3, [sp, #-1*8]
	str x4, [sp, #-2*8]

	mrs   x1, hcr_el2  /* read HCR register */
	tst   x1, #1       /* check VM bit      */
	beq   _host_to_vm

	_host_ptr          /* in x3 - x3, x4 changed */
	add x3, x3, #6*8   /* Host_context->vm_state */

	ldr x1, [sp, #-0*8]
	str x0, [sp, #-0*8]

	ldr x0, [x3]       /* x0 contains vm_state pointer */

	ldr x3, [sp, #-1*8]
	ldr x4, [sp, #-2*8]

	b     _vm_to_host
.endm


.section .text

_unknown_exception:
	1: yield
	b 1b

/*
 * see D1.10.2 Exception vectors chapter
 */
.p2align 12
.global el2_exception_vector
el2_exception_vector:
	.rept 0x100
		b _unknown_exception /* 4 bytes each branch */
	.endr
	_vm_exit 0   /* offset at 0x400 */
	.rept 0x3fc - 1 /* 0x1000 - 4 - (0x100 * 4) */
		b _unknown_exception /* 4 bytes each branch */
	.endr


_host_to_vm:

	add        x0, x0, #31*8  /* skip x0...x30, loaded later */

	/* 64 bit values */
	ldr  x1, [x0], #1*8       /* Cpu_state->sp */
	ldp  x2,  x3, [x0, #0*8]  /* Cpu_state->ip,       Vm_state->sctlr_el1 */
	ldp  x4,  x5, [x0, #2*8]  /* Vm_state->ttbr0_el1, Vm_state->ttbr1_el1 */
	ldp  x6,  x7, [x0, #4*8]  /* Vm_state->tcr_el1,   Vm_state->elr_el1 */
	ldp  x8,  x9, [x0, #6*8]  /* Vm_state->sp_el0,    Vm_state->far_el1 */
	ldp x10, x11, [x0, #8*8]  /* Vm_state->mair_el1,  Vm_state->vbar_el1 */
	ldp x12, x13, [x0, #10*8] /* Vm_state->actlr_el1, Vm_state->amair_el1 */
	ldp x14, x15, [x0, #12*8] /* Vm_state->par_el1,   Vm_state->tpidrro_el0 */
	ldp x16, x17, [x0, #14*8] /* Vm_state->tpidr_el0, Vm_state->tpidr_el1 */

	/* 32 bit values */
	ldp w18, w19, [x0, #16*8] /* Vm_state->spsel,     Vm_state->spsr_el1 */
	ldp w20, w21, [x0, #17*8] /* Vm_state->daif,      Vm_state->nzcv */
	ldp w22, w23, [x0, #18*8] /* Vm_state->fpcr,      Vm_state->fpsr */
	ldp w24, w25, [x0, #19*8] /* Vm_state->esr_el1,   Vm_state->cpacr_el1 */
	ldp w26, w27, [x0, #20*8] /* Vm_state->afsr0_el1, Vm_state->afsr1_el1 */
	ldp w28, w29, [x0, #21*8] /* Vm_state->contextidr_el1, Vm_state->csselr_el1 */

	/* 64 bit values */
	msr sp_el1,      x1
	msr elr_el2,     x2
	msr sctlr_el1,   x3
	msr ttbr0_el1,   x4
	msr ttbr1_el1,   x5
	msr tcr_el1,     x6
	msr elr_el1,     x7
	msr sp_el0,      x8
	msr far_el1,     x9
	msr mair_el1,    x10
	msr vbar_el1,    x11
	msr actlr_el1,   x12
	msr amair_el1,   x13
	msr par_el1,     x14
	msr tpidrro_el0, x15
	msr tpidr_el0,   x16
	msr tpidr_el1,   x17

	/* 32 bit values */
	msr spsel,          x18
	msr spsr_el1,       x19
	msr daif,           x20
	msr nzcv,           x21
	msr fpcr,           x22
	msr fpsr,           x23
	msr esr_el1,        x24
	msr cpacr_el1,      x25
	msr afsr0_el1,      x26
	msr afsr1_el1,      x27
	msr contextidr_el1, x28
	msr csselr_el1,     x29

	add  x0,  x0, #24 * 8
	ldp  q0,  q1,  [x0, #32 * 0]
	ldp  q2,  q3,  [x0, #32 * 1]
	ldp  q4,  q5,  [x0, #32 * 2]
	ldp  q6,  q7,  [x0, #32 * 3]
	ldp  q8,  q9,  [x0, #32 * 4]
	ldp  q10, q11, [x0, #32 * 5]
	ldp  q12, q13, [x0, #32 * 6]
	ldp  q14, q15, [x0, #32 * 7]
	ldp  q16, q17, [x0, #32 * 8]
	ldp  q18, q19, [x0, #32 * 9]
	ldp  q20, q21, [x0, #32 * 10]
	ldp  q22, q23, [x0, #32 * 11]
	ldp  q24, q25, [x0, #32 * 12]
	ldp  q26, q27, [x0, #32 * 13]
	ldp  q28, q29, [x0, #32 * 14]
	ldp  q30, q31, [x0, #32 * 15]
	sub  x0,  x0, #24 * 8

	_host_ptr                 /* in x3 - x3, x4 changed */
	add        x3, x3, #7*8   /* vttbr_el2 address in x3 */
	ldr        x1, [x3]       /* Host_context->vttbr_el2 */
	msr vttbr_el2, x1

	mrs        x1, hcr_el2     /* enable VM mode */
	orr        x1, x1, #1
	msr   hcr_el2, x1

	sub        x0, x0, #32*8   /* mov to head of Cpu_state */
	mov       x30, x0

	ldp   x0,  x1, [x30], #2*8
	ldp   x2,  x3, [x30], #2*8
	ldp   x4,  x5, [x30], #2*8
	ldp   x6,  x7, [x30], #2*8
	ldp   x8,  x9, [x30], #2*8
	ldp  x10, x11, [x30], #2*8
	ldp  x12, x13, [x30], #2*8
	ldp  x14, x15, [x30], #2*8
	ldp  x16, x17, [x30], #2*8
	ldp  x18, x19, [x30], #2*8
	ldp  x20, x21, [x30], #2*8
	ldp  x22, x23, [x30], #2*8
	ldp  x24, x25, [x30], #2*8
	ldp  x26, x27, [x30], #2*8
	ldp  x28, x29, [x30], #2*8
	ldr  x30, [x30]

	eret

_vm_to_host:

	add        x0,   x0, #1*8    /* Cpu_state->r0 skip, later, see below */
	str        x1, [x0], #1*8    /* Cpu_state->r1 */
	stp   x2,  x3, [x0], #2*8    /* Cpu_state->r2 & r3 */
	stp   x4,  x5, [x0], #2*8    /* ... */
	stp   x6,  x7, [x0], #2*8
	stp   x8,  x9, [x0], #2*8
	stp  x10, x11, [x0], #2*8
	stp  x12, x13, [x0], #2*8
	stp  x14, x15, [x0], #2*8
	stp  x16, x17, [x0], #2*8
	stp  x18, x19, [x0], #2*8
	stp  x20, x21, [x0], #2*8
	stp  x22, x23, [x0], #2*8
	stp  x24, x25, [x0], #2*8
	stp  x26, x27, [x0], #2*8    /* ... */
	stp  x28, x29, [x0], #2*8    /* Cpu_state->r28 & r29 */
	str       x30, [x0], #1*8    /* Cpu_state->r30 */

	ldr x1, [sp]                 /* move guest x0 on stack to Cpu_state->r0 */
	str x1, [x0, #-31*8]

	mrs x1, sp_el1               /* Cpu_state->sp */
	str x1, [x0], #1*8

	/* 64 bit registers */
	mrs  x1, elr_el2
	mrs  x2, sctlr_el1
	mrs  x3, ttbr0_el1
	mrs  x4, ttbr1_el1
	mrs  x5, tcr_el1
	mrs  x6, elr_el1
	mrs  x7, sp_el0
	mrs  x8, far_el1
	mrs  x9, mair_el1
	mrs x10, vbar_el1
	mrs x11, actlr_el1
	mrs x12, amair_el1
	mrs x13, par_el1
	mrs x14, tpidrro_el0
	mrs x15, tpidr_el0
	mrs x16, tpidr_el1

	/* 32 bit registers */
	mrs x17, spsel
	mrs x18, spsr_el1
	mrs x19, daif
	mrs x20, nzcv
	mrs x21, fpcr
	mrs x22, fpsr
	mrs x23, esr_el1
	mrs x24, cpacr_el1
	mrs x25, afsr0_el1
	mrs x26, afsr1_el1
	mrs x27, contextidr_el1
	mrs x28, csselr_el1

	/* 64 bit registers */
	stp  x1,  x2, [x0, #0*8]  /* Cpu_state->ip, Vm_state->sctlr_el1 */
	stp  x3,  x4, [x0, #2*8]  /* Vm_state->ttbr0_el1, Vm_state->ttbr1_el1 */
	stp  x5,  x6, [x0, #4*8]  /* Vm_state->tcr_el1, Vm_state->elr_el1 */
	stp  x7,  x8, [x0, #6*8]  /* Vm_state->sp_el0, Vm_state->far_el1 */
	stp  x9, x10, [x0, #8*8]  /* Vm_state->mair_el1, Vm_state->vbar_el1 */
	stp x11, x12, [x0, #10*8] /* Vm_state->actlr_el1, Vm_state->amair_el1 */
	stp x13, x14, [x0, #12*8] /* Vm_state->par_el1, Vm_state->tpidrro_el0 */
	stp x15, x16, [x0, #14*8] /* Vm_state->tpidr_el0, Vm_state->tpidr_el1 */

	/* 32 bit registers */
	stp w17, w18, [x0, #16*8] /* Vm_state->spsel, Vm_state->spsr_el1 */
	stp w19, w20, [x0, #17*8] /* Vm_state->daif, Vm_state->nzcv */
	stp w21, w22, [x0, #18*8] /* Vm_state->fpcr, Vm_state->fpsr */
	stp w23, w24, [x0, #19*8] /* Vm_state->esr_el1, Vm_state->cpacr_el1 */
	stp w25, w26, [x0, #20*8] /* Vm_state->afsr0_el1, Vm_state->afsr1_el1 */
	stp w27, w28, [x0, #21*8] /* Vm_state->contextidr_el1, Vm_state->csselr_el1 */

	add  x0,  x0, #24 * 8
	stp  q0,  q1,  [x0], #32
	stp  q2,  q3,  [x0], #32
	stp  q4,  q5,  [x0], #32
	stp  q6,  q7,  [x0], #32
	stp  q8,  q9,  [x0], #32
	stp  q10, q11, [x0], #32
	stp  q12, q13, [x0], #32
	stp  q14, q15, [x0], #32
	stp  q16, q17, [x0], #32
	stp  q18, q19, [x0], #32
	stp  q20, q21, [x0], #32
	stp  q22, q23, [x0], #32
	stp  q24, q25, [x0], #32
	stp  q26, q27, [x0], #32
	stp  q28, q29, [x0], #32
	stp  q30, q31, [x0], #32

	_host_ptr                 /* in x3 */
	ldp x4, x5, [x3], #2*8    /* Host_context->sp, Host_context->ip */
	mov sp, x4
	msr elr_el2, x5

	ldp  x4,  x5, [x3], #2*8  /* Host_context->ttbr0, Host_context->ttbr1 */
	ldp  x6,  x7, [x3], #4*8  /* sctlr, tcr + skip vmstate, vttbr_el2 */

	mrs x8, esr_el2           /* write to Host_context->esr_el2 */
	str x8, [x3], #1*8

	ldp  x8,  x9, [x3], #2*8  /* Host_context->elr_el1, sp_el0 */

	ldp x10, x11, [x3], #2*8  /* Host_context->mair_el1, vbar_el1 */
	ldp x12, x13, [x3], #2*8  /* Host_context->actlr_el1, amair_el1 */
	ldp x14, x15, [x3], #2*8  /* Host_context->par_el1, tpidrro_el0 */
	ldp x16, x17, [x3], #2*8  /* Host_context->tpidr_el0, tpidr_el1 */

	ldp w18, w19, [x3], #2*4  /* Host_context->spsel, spsr_el1 */
	ldp w20, w21, [x3], #2*4  /* Host_context->daif, nzcv */
	ldp w22, w23, [x3], #2*4  /* Host_context->fpcr, fpsr */
	ldp w24, w25, [x3], #2*4  /* Host_context->cpacr_el1, afsr0_el1 */
	ldp w26, w27, [x3], #2*4  /* Host_context->afsr1_el1, contextidr_el1 */
	ldr      w28, [x3], #2*4  /* Host_context->csselr_el1 */

	msr ttbr0_el1,      x4
	msr ttbr1_el1,      x5
	msr sctlr_el1,      x6
	msr tcr_el1  ,      x7
	msr elr_el1  ,      x8
	msr sp_el0   ,      x9
	msr mair_el1 ,      x10
	msr vbar_el1 ,      x11
	msr actlr_el1,      x12
	msr amair_el1,      x13
	msr par_el1  ,      x14
	msr tpidrro_el0,    x15
	msr tpidr_el0,      x16
	msr tpidr_el1,      x17
	msr spsel    ,      x18
	msr spsr_el1 ,      x19
	msr daif     ,      x20
	msr nzcv     ,      x21
	msr fpcr     ,      x22
	msr fpsr     ,      x23
	msr cpacr_el1,      x24
	msr afsr0_el1,      x25
	msr afsr1_el1,      x26
	msr contextidr_el1, x27
	msr csselr_el1,     x28

	# far_el1 not restored for hw kernel
	# esr_el1 not restored for hw kernel

	mrs x0, hcr_el2     /* switch VM bit off */
	bic x0, x0, #1
	msr hcr_el2, x0

	eret

/* host kernel must jump to this point to switch to a vm */
.global hypervisor_enter_vm
hypervisor_enter_vm:
	hvc #0

_vt_host_context_ptr: .quad vt_host_context
