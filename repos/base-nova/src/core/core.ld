/*
 * \brief  Linker script for Genode programs
 * \author Christian Helmuth
 * \date   2006-04-12
 */

/*
 * Copyright (C) 2006-2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/*
 * NOVA-specific change: NOVA does not support the BSS segment for
 * roottask. Therefore, we have to place all BSS content into the
 * data section.
 */

ENTRY(_start)

PHDRS
{
	ro PT_LOAD FLAGS(5);
	rw PT_LOAD FLAGS(6);
	boot PT_LOAD FLAGS(4);
}

SECTIONS
{
	.text : {
		/* begin of program image (link address) */
		_prog_img_beg = .;

		/* put entry code at the start of the text segment / raw binary */
		KEEP (*(.text.crt0))

		*(.init)
		*(.text .text.* .gnu.linkonce.t.*)
		*(.fini)
		*(.rodata .rodata.* .gnu.linkonce.r.*)

		. = ALIGN(0x08);

		_ctors_start = .;
		KEEP (*(.ctors))
		KEEP (*(SORT(.ctors.*)))
		KEEP (*(.init_array)) /* list of constructors specific for ARM eabi */
		_ctors_end = .;
		_dtors_start = .;
		KEEP (*(SORT(.dtors.*)))
		KEEP (*(.dtors))
		_dtors_end = .;
	} : ro = 0x0

	/* Linux: exception section for uaccess mechanism */
	__ex_table : { *(__ex_table) }

	.eh_frame_hdr : { *(.eh_frame_hdr) }

	. = ALIGN(0x1000);

	.data : {
		*(.data .gnu.linkonce.d.*)
		/* include all data subsections except those of the boot modules */
		*(EXCLUDE_FILE (*boot_modules.o) .data.*)
		. = ALIGN(8);
		*(.bss .bss.* .gnu.linkonce.b.* COMMON)
	} : rw

	/* exception frames for C++ */
	.eh_frame : {
		__eh_frame_start__ = .;
		KEEP (*(.eh_frame))
		LONG(0)
	} : rw

	.gcc_except_table : {
		KEEP(*(.gcc_except_table))
		KEEP(*(.gcc_except_table.*))
	}

	.dynamic :  { *(.dynamic) }

	/* separate location for the binaries of the boot modules */
	.data.boot_modules_binaries : { *(.data.boot_modules_binaries) } : boot

	/* end of program image -- must be after last section */
	_prog_img_end = .;

	/DISCARD/ : {
		*(.note)
		*(.note.ABI-tag)
		*(.comment)
	}
}
