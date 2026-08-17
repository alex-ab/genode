/*
 * \brief  Interpolate an expensive remote clock with a cheap time source
 * \author Johannes Schlatow
 * \date   2026-07-10
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <util/local_clock.h>

using namespace Genode;

Local_clock::Interval::Interval(Local_clock::Data_point start,
                                Local_clock::Data_point end)
{
	Remote_clock const us_diff  { max(1ULL, end.remote_clock.us - start.remote_clock.us) };
	Tsc          const tsc_diff { max(1ULL, end.tsc.ticks       - start.tsc.ticks) };

	/* this should never happen for TSC frequencies > 1MHz */
	if (tsc_diff.ticks < us_diff.us) {
		error("Local_clock: TSC appears to tick slower than 1MHz");
		ticks_per_ms = 1;
	}
	else
		ticks_per_ms = tsc_diff.ticks * 1000 / us_diff.us;

	error_ppm = ((start.error.ticks + end.error.ticks) * 1000 * 1000)
	            / tsc_diff.ticks;
}


void Local_clock::Stats::_drop_half()
{
	count /= 2;
	sum_of_squared_deviations /= 2;
}


void Local_clock::Stats::add(Local_clock::Interval i)
{
	if (count > MAX_COUNT)
		_drop_half();

	/*
	 * Welford's algorithms for incremental computation of mean and variance
	 */

	count++;
	uint64_t const old_mean = mean_ticks_per_ms;
	mean_ticks_per_ms -= (mean_ticks_per_ms / count);
	mean_ticks_per_ms += (i.ticks_per_ms    / count);

	/* handle integer overflow */
	uint64_t const increment = _abs_diff(i.ticks_per_ms, old_mean) *
	                           _abs_diff(i.ticks_per_ms, mean_ticks_per_ms);
	while (increment > MAX_VALUE - sum_of_squared_deviations) {
		warning("Local_clock: variance tracking reset due to integer overflow");
		_drop_half();
	}

	sum_of_squared_deviations += increment;

	if (count < 2)
		return;

	/*
	 * calculate standard deviation = sqrt(variance) based on Newton-Raphson
	 */
	Tsc const variance { sum_of_squared_deviations / (count - 1) };
	if (variance.ticks < 2) 
		standard_deviation = variance.ticks;
	else {
		/*
		 * Assuming TSC deviations in the GHz range, we get a variance in
		 * the order of magnitude of 10^6. 13 iterations seem enough
		 * for the sqrt to converge sufficiently. If it does not converge,
		 * we will merely overestimate the standard deviation. Since,
		 * we use the last computed sqrt as a starting point, the number
		 * of necessary iterations is much lower. We stop if the difference
		 * to the last iteration is less than 0.1% of the mean.
		 */
		uint64_t last = standard_deviation;
		for (unsigned i=0; i < 13 && standard_deviation > 0; i++) {
			standard_deviation =
				(standard_deviation + variance.ticks / standard_deviation) / 2;

			uint64_t const diff =
				last < standard_deviation ? standard_deviation - last
			                            : last - standard_deviation;
			if (diff <= max(1ULL, mean_ticks_per_ms / 1000ULL))
				break;
		}
	}

	/* prevent division by 0 */
	standard_deviation = max(1ULL, standard_deviation);

	/* calculate refresh interval */
	uint64_t const refresh_us = MAX_DRIFT_US * mean_ticks_per_ms / standard_deviation;
	refresh_interval          = Tsc { refresh_us * mean_ticks_per_ms / 1000 };
}


bool Local_clock::_clock_increasing(Remote_clock first, Remote_clock second)
{
	if (first.us < second.us)
		return true;

	if (first.us > second.us && _errors_logged < MAX_ERRORS)
		error("Local_clock: remote clock not monotonically increasing");

	return false;
}


bool Local_clock::_tsc_increasing(Tsc first, Tsc second)
{
	if (first.ticks <= second.ticks)
		return true;

	if (_errors_logged < MAX_ERRORS) {
		uint64_t const us_drift = _us_diff(second, first,
		                                   max(1ULL, _stats.mean_ticks_per_ms));
		if (us_drift > MAX_DRIFT_US) {
			warning("Timestamps are out-of-sync or overflowed, potential time drift of ", us_drift, "us.");
			_errors_logged++;
		}
	}

	return false;
}


void Local_clock::_update_interpolated_clock(Tsc current_tsc)
{
	Remote_clock new_interpolated_clock {
		_last_sync->remote_clock.us + _us_diff(_last_sync->tsc,
		                                       current_tsc,
		                                       _stats.mean_ticks_per_ms) };

	_interpolated_clock.us = max(new_interpolated_clock.us,
	                             _interpolated_clock.us);
}


void Local_clock::add_data_point(Remote_clock remote_clock,
                                 Tsc tsc_start, Tsc tsc_end)
{
	Tsc const error { (tsc_end.ticks - tsc_start.ticks) / 2 };
	Data_point const data_point { .remote_clock = remote_clock,
	                              .tsc          = tsc_start.ticks + error.ticks,
	                              .error        = error };

	if (_last_sync.constructed()) {
		if(!_clock_increasing(_last_sync->remote_clock, data_point.remote_clock))
			return;
		if(!_tsc_increasing(_last_sync->tsc, data_point.tsc))
			return;

		Interval const interval { *_last_sync, data_point };
		if (interval.error_ppm <= MAX_ERROR_PPM)
			_stats.add(interval);
		else if (_last_sync->error.ticks < data_point.error.ticks)
			return; /* keep better data point */
	}

	_last_sync.construct(data_point);
}
