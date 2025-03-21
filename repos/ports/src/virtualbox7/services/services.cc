/*
 * \brief  Service backend helper
 * \author Christian Helmuth
 * \author Sebastian Sumpf
 * \date   2021-09-01
 *
 * This module stores the Genode environment reference for service shared
 * objects that require Genode connections (e.g., shared-clipboard reports
 * and ROMs).
 */

/*
 * Copyright (C) 2021-2026 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

/* Genode includes */
#include <base/env.h>

/* local includes */
#include <services/services.h>

using namespace Genode;

Constructible<Services> services;

Env & Services::env(){ return _env; }


void Services::focus_change(bool focus)
{
	_focus = focus;

}


void Services::focus_active()
{
	if (!_focus) return;

	if (_focus_handler)
		_focus_handler->local_submit();

	_focus = false;
}


void Services::focus_handler(Signal_context *handler)
{
	_focus_handler = handler;
}


Services::Services(Env &env) : _env(env) { }


void Services::init(Env &env)
{
	if (services.constructed()) return;

	services.construct(env);
}


Services &Services::service()
{
	if (!services.constructed())
		error("Service is not constructed");

	return *services;
}

