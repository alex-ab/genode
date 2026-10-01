/*
 * \brief  Minimal Genode emulation for HID/XML utilities
 * \author Christian Helmuth
 * \date   2026-10-01
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Linux includes */
#include <cstdio>

/* Genode includes */
#include <base/log.h>

namespace Genode {

	void Mutex::acquire() { }
	void Mutex::release() { }

	Lock::Lock(Lock::State) : _owner(nullptr) { }

	void Log::_acquire(Type t)
	{
		_type = t;

		switch (_type) {
		case LOG:                                              break;
		case WARNING: _output.out_string("\033[34mWarning: "); break;
		case ERROR:   _output.out_string("\033[31mError: ");   break;
		};
	}

	void Log::_release()
	{
		switch (_type) {
		case LOG:     _output.out_string("\n");        break;
		case WARNING:
		case ERROR:   _output.out_string("\033[0m\n"); break;
		};

		_type = { };
	}

	Log & Log::log()
	{
		struct Write_fn { void operator () (char const *s) { ::printf("%s", s); } };

		static Buffered_output<512, Write_fn> b { Write_fn() };
		static Log                            l { b };

		return l;
	}

	void Raw::_acquire() { _output().out_string("\033[32m"); }
	void Raw::_release() { _output().out_string("\033[0m\n"); }

	Output & Raw::_output()
	{
		struct Write_fn { void operator () (char const *s) { ::fprintf(stderr, "%s", s); } };

		static Buffered_output<512, Write_fn> b { Write_fn() };

		return b;
	}
}
