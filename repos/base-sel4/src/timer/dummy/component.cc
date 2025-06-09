/*
 * \brief  Dummy timer driver - not functional by any means
 * \author Alexander Boettcher
 * \date   2025-07-05
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>

namespace Timer {

	using namespace Genode;

	struct Main;
}

struct Timer::Main
{
	Env &_env;

	Attached_rom_dataspace _config { _env, "config" };

	Main(Genode::Env &env) : _env(env)
	{
		error("dummy timer driver - ",
		      "on seL4 you have to implement one for your target platform");
	}
};


void Component::construct(Genode::Env &env) { static Timer::Main inst(env); }
