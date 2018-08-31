/*
 * \brief  Show ROM content
 * \author Alexander Boettcher
 * \date   2018-08-30
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* base includes */
#include <base/component.h>
#include <log_session/connection.h>

/* os includes */
#include <base/attached_rom_dataspace.h>
#include <timer_session/connection.h>


struct Monitor
{
	Genode::Env &env;

	Timer::Connection timer { env };

	Genode::Signal_handler<Monitor> check_rom { env.ep(), *this, &Monitor::check };

	Genode::Attached_rom_dataspace pds { env, "pds_stats" };

	Monitor(Genode::Env &env) : env(env)
	{
		Genode::addr_t period_ms = 1000;

		try {
			Genode::Attached_rom_dataspace config { env, "config" };
			period_ms = config.xml().attribute_value("period_ms", 1000UL);
		} catch (...) { }

		if (period_ms) {
			timer.sigh(check_rom);
			timer.trigger_periodic(1000UL * period_ms);
		}

		pds.sigh(check_rom);
	}

	void check()
	{
		Genode::log("updated PD ROM statistics:");
		Genode::log(pds.local_addr<const char>());
	}
};


void Component::construct(Genode::Env &env) { static Monitor output(env); }
