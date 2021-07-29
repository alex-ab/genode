/**
 * \brief  Unsupported generations on Genode for i965 mesa driver
 * \author Alexander Boettcher
 * \date   2021-07-29
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */
#include <base/log.h>
#include <base/sleep.h>

#define DUMMY(name) \
extern "C" void name(void) \
{ \
	Genode::error(__func__, ": unsupported generation"); \
	Genode::sleep_forever(); \
}

DUMMY( gen4_blorp_exec)
DUMMY(gen45_blorp_exec)
DUMMY( gen5_blorp_exec)
DUMMY( gen6_blorp_exec)
DUMMY( gen7_blorp_exec)
DUMMY(gen75_blorp_exec)

DUMMY( gen4_emit_raw_pipe_control)
DUMMY(gen45_emit_raw_pipe_control)
DUMMY( gen5_emit_raw_pipe_control)
DUMMY( gen6_emit_raw_pipe_control)
DUMMY( gen7_emit_raw_pipe_control)
DUMMY(gen75_emit_raw_pipe_control)
