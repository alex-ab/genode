/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <stdarg.h>

#include <base/snprintf.h>
#include <log_session/log_session.h>

void vprintf(const char *format, va_list &args)
{
	using namespace Genode;

	static char buf[Log_session::MAX_STRING_LEN-4];

	String_console sc(buf, sizeof(buf));
	sc.vprintf(format, args);

	int n = sc.len();
	if (0 < n && buf[n-1] == '\n') n--;

	log("VMM: ", Cstring(buf, n));
}

extern "C"
void AcpiOsPrintf (const char *format, ...)
{
	va_list list;
	va_start(list, format);

	::vprintf(format, list);

	va_end(list);
}

extern "C"
void AcpiOsVprintf (const char *format, va_list &va)
{
	::vprintf(format, va);
}

