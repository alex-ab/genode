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
#include <kernel/interface.h>
#include <timer_session/connection.h>

using namespace Genode;
using namespace Genode::Trace;

void Timer::Connection::_set_alarm(Duration deadline)
{
	Tsc const start_ts = Tsc { Kernel::time() };
	_last_clock_value  = Remote_clock { trigger_at(deadline.trunc_to_plain_us().value) };
	Tsc const end_ts   = Tsc { Kernel::time() };
	_local_clock.add_data_point(_last_clock_value, start_ts, end_ts);
}


Duration Timer::Connection::curr_time()
{
	_switch_to_timeout_framework_mode();

	Mutex::Guard guard (_local_clock_mutex);

	_last_clock_value = _local_clock.predicted(
		[&] () -> Remote_clock { return Remote_clock { elapsed_us() };   },
		[&] () -> Tsc          { return Tsc          { Kernel::time() }; });
	
	return Duration { Microseconds { _last_clock_value.us }};
}
