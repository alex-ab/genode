/*
 * \brief  Test Local_clock utility
 * \author Johannes Schlatow
 * \date   2026-07-32
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License ve sion 3.
 */

/* Genode includes */
#include <util/local_clock.h>
#include <timer_session/connection.h>

using namespace Genode;

/*
 * External hook for arch-specific sleeping policy
 */
void sleep_or_busy_loop(size_t             iteration,
                        Duration           duration,
                        Timer::Connection &timer,
                        Local_clock       &)
{
	/*
	 * Only sleep the given duration every other iteration. This ensures that
	 * we can predict about 50% of calls because they are not too distant in time.
	 * However, if two calls of 'predicted()' are too close together, we cannot
	 * reliably measure the relative drift because the latency of 'elapsed_us()'
	 * will contribute most of the error.
	 */
   
	if (iteration % 2)
		timer.msleep(duration.trunc_to_plain_ms().value);
	else
	  timer.msleep(1);
}
