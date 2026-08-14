/*
 * \brief  Predict an expensive remote clock with a cheap local time source
 * \author Johannes Schlatow
 * \date   2026-07-10
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _UTIL__LOCAL_CLOCK_H_
#define _UTIL__LOCAL_CLOCK_H_

/* Genode includes */
#include <base/fixed_stdint.h>
#include <util/reconstructible.h>

namespace Genode {

	struct Remote_clock;
	struct Tsc;
	class  Local_clock;
}


struct Genode::Remote_clock
{
	uint64_t us;
};


struct Genode::Tsc
{
	uint64_t ticks;
};


class Genode::Local_clock
{
	private:

		static constexpr unsigned MAX_ERRORS    = 10;
		static constexpr unsigned MAX_ERROR_PPM = 20000;
		static constexpr uint64_t MAX_DRIFT_US  = 1000;

		struct Data_point
		{
			Remote_clock remote_clock;
			Tsc          tsc;
			Tsc          error;
		};


		struct Interval
		{
			uint64_t ticks_per_ms { 0 };
			uint64_t error_ppm    { 0 };

			Interval(Data_point start, Data_point end);
		};


		/*
		 * Incremental tracking of mean and variance following Welford's algorithm
		 */
		struct Stats
		{
			size_t   count                     { 0 };
			uint64_t mean_ticks_per_ms         { 0 };
			uint64_t sum_of_squared_deviations { 0 };
			uint64_t standard_deviation        { 1000 };
			Tsc      refresh_interval          { 0 };

			/*
			 * With a TSC frequency of at least 100MHz, ticks_per_ms is at
			 * least ~100.000. We set a limit for count (i.e. the number of samples)
			 * in order to make sure that the integer division of ticks_per_ms by
			 * count does remain accurate enough. Moreover, the limit makes sure
			 * that changes in TSC frequency (due to frequency scaling), have a
			 * noticeable effect on the mean.
			 */
			static constexpr size_t   MAX_COUNT = 100;
			static constexpr uint64_t MAX_VALUE = ~0ULL;

			inline uint64_t _abs_diff(uint64_t a, uint64_t b)
			{
				if (a > b)
					return a - b;
				else
					return b - a;
			}

			inline Tsc next_sync(Tsc last_sync_tsc) const {
				return Tsc { last_sync_tsc.ticks + refresh_interval.ticks }; }

			void _drop_half();
			void add(Interval i);
		};

		Constructible<Data_point> _last_sync { };
		Remote_clock              _interpolated_clock { 0 };
		Stats                     _stats { };
		unsigned                  _errors_logged { 0 };

		uint64_t                  _num_predicted    { 0 };
		uint64_t                  _num_interpolated { 0 };

		inline uint64_t _us_diff(Tsc first, Tsc second, uint64_t ticks_per_ms) {
			return ((second.ticks - first.ticks) * 1000) / ticks_per_ms; }

		bool _clock_increasing(Remote_clock first, Remote_clock second);

		bool _tsc_increasing(Tsc first, Tsc second);

		/*
		 * Assumes _last_sync.constructed() and current_tsc > _last_sync->tsc
		 */
		void _update_interpolated_clock(Tsc current_tsc);

		void _add_data_point(Remote_clock remote_clock, Tsc tsc_start, Tsc tsc_end);

	public:

		/*
		 * Discard the last data point. This is necessary, e.g., after the CPU
		 * went into a sleep state that halted the timestamp counter.
		 */
		void unsync() { _last_sync.destruct(); }

		/*
		 * Locally predict remote clock value based on a cheap 'tsc_fn' and an
		 * expensive 'remote_clock_fn'. If enough data points have been taken
		 * and the last call to 'remote_clock_fn' is not too old, 'tsc_fn' will
		 * be used for prediction.
		 */
		Remote_clock predicted(auto const &remote_clock_fn, auto const &tsc_fn)
		{
			_num_predicted++;

			/* interpolate if last sync is not too far away (depending on variance) */
			if (_last_sync.constructed()) {
				Tsc const tsc = tsc_fn();
				if (!_tsc_increasing(_last_sync->tsc, tsc))
					_last_sync.destruct();
				else if (tsc.ticks < _stats.next_sync(_last_sync->tsc).ticks) {
					_num_interpolated++;
					_update_interpolated_clock(tsc);
					return _interpolated_clock;
				}
			}

			/*
			 * We assume that we are able to interpolate at least 20% of all
			 * calls. Otherwise, let's print a message.
			 */
			if (_stats.count > 20 && _num_interpolated * 100 / _num_predicted < 20) {
				warning("Local_clock: interpolation is ineffective. " \
				        "This might be caused by TSC instability.");
				_errors_logged++;
			}

			/* take new data point */
			Tsc          const tsc_start    = tsc_fn();
			Remote_clock const remote_clock = remote_clock_fn();
			Tsc          const tsc_end      = tsc_fn();
			_add_data_point(remote_clock, tsc_start, tsc_end);

			/* make sure that returned clock is monotonically increasing */
			_interpolated_clock.us = max(remote_clock.us, _interpolated_clock.us);
			return _interpolated_clock;
		}
};

#endif /* _UTIL__LOCAL_CLOCK_H_ */
