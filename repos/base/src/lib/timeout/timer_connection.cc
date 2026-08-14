/*
 * \brief  Connection to timer service and timeout scheduler
 * \author Martin Stein
 * \date   2016-11-04
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <timer_session/connection.h>

using namespace Genode;


void Timer::Connection::_handle_timeout()
{
	if (_timeout_scheduler.constructed())
		_timeout_scheduler->handle_timeout(curr_time());
}


void Timer::Connection::set_alarm(Duration deadline)
{
	/* use result of preceding curr_time() */
	Duration now = Duration { Microseconds { _last_clock_value.us } };
	now.add(Microseconds { TIMEOUT_ACCURACY_US });

	/* trigger locally, if deadline already passed */
	if (deadline.less_than(now)) {
		_signal_handler.local_submit();
		return;
	}

	_set_alarm(deadline);
}


Timer::Connection::Connection(Env &env, Entrypoint &ep, Label const &label)
:
	Genode::Connection<Session>(env, label, Ram_quota { 10*1024 }, Args()),
	Session_client(cap()),
	_ep(ep)
{
	/* register default signal handler */
	Session_client::sigh(_default_sigh_cap);
}


Timeout_scheduler &Timer::Connection::_switch_to_timeout_framework_mode()
{
	if (_timeout_scheduler.constructed())
		return *_timeout_scheduler;

	_sigh(_signal_handler);

	_timeout_scheduler.construct(*(Time_source*)this, Microseconds { TIMEOUT_ACCURACY_US });

	return *_timeout_scheduler;
};
