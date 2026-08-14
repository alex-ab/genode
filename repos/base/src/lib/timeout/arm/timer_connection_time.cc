/*
 * \brief  Connection to timer service and timeout scheduler
 * \author Martin Stein
 * \date   2016-11-04
 *
 * On ARM, we do not have a component-local hardware time-source. The ARM
 * performance counter has no reliable frequency as the ARM idle command
 * halts the counter. Thus, we do not do local time interpolation.
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

void Timer::Connection::_set_alarm(Duration deadline)
{
	_last_clock_value  = Remote_clock { trigger_at(deadline.trunc_to_plain_us().value) };
}


Duration Timer::Connection::curr_time()
{
  _last_clock_value = Remote_clock { elapsed_us() };
  return Duration { Microseconds { _last_clock_value.us }};
}
