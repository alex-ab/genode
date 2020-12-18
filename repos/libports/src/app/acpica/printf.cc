/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <stdarg.h>
#include <stdio.h>

#include <libc/component.h>

extern "C"
void AcpiOsPrintf (const char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);

	Libc::with_libc([&] () {
		vprintf(fmt, va);
	});

	va_end(va);
}

extern "C"
void AcpiOsVprintf (const char *fmt, va_list va)
{
	Libc::with_libc([&] () {
		vprintf(fmt, va);
	});
}
