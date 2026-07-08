/*
 * \brief  Multiplexing one time source amongst different timeout subjects
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
#include <timer/timeout.h>
#include <timer_session/connection.h>

using namespace Genode;


/*************
 ** Timeout **
 *************/

void Timeout::schedule_periodic(Microseconds duration)
{
	/* prevent using a period of 0 */
	if (duration.value == 0) {
		error("attempt to schedule a periodic timeout of 0");
		return;
	}

	_scheduler._schedule_timeout(*this, Microseconds { 0 }, duration);
}


void Timeout::schedule_one_shot(Microseconds duration)
{
	_scheduler._schedule_timeout(*this, duration, Microseconds { 0 });
}


Timeout::Timeout(Timeout_scheduler &scheduler, Timeout_handler &handler)
:
	_scheduler(scheduler), _handler(handler)
{ }


Timeout::Timeout(Timer::Connection &timer_connection, Timeout_handler &handler)
:
	_scheduler(timer_connection._switch_to_timeout_framework_mode()),
	_handler(handler)
{ }


Timeout::~Timeout()
{
	discard();
}

void Timeout::discard()
{
	Mutex::Guard scheduler_guard { _scheduler._mutex };
	_scheduler._discard_timeout_unsynchronized(*this);
}

bool Timeout::scheduled() { return _alarm.constructed(); }

Duration Timeout::deadline() const
{
	return _alarm.constructed() ? Duration { Microseconds { _alarm->time.value() } }
	                            : Duration { Microseconds { 0 } };
}


/***********************
 ** Timeout_scheduler **
 ***********************/

void Timeout_scheduler::handle_timeout(Duration curr_time)
{
	_mutex.acquire();

	Clock lookahead_time { curr_time };
	lookahead_time.add(_accuracy_us);

	bool handled { true };

	while (handled) {
		handled = _alarms.with_any_in_range(Clock { 0 }, lookahead_time,
			[&] (Alarm &alarm) {
				Timeout &timeout { alarm.timeout };
				Clock deadline   { alarm.time };

				timeout._alarm.destruct();

				if (timeout._in_discard_blockade)
					return;

				timeout._in_handler = true;

				/*
				 * Timeout handlers are called without holding the scheduler mutex.
				 * This ensures that the handler can, for instance, re-schedule
				 * the timeout without running into a deadlock.
				 * The only thing we synchronize is discarding the
				 * timeout.
				 */

				_mutex.release();

				timeout._handler.handle_timeout(curr_time);

				_mutex.acquire();

				if (timeout._period.value > 0) {

					while (deadline.earlier(curr_time))
						deadline.add(timeout._period);

					/* re-insert periodic timeout */
					timeout._alarm.construct(_alarms, timeout, deadline);
				}

				timeout._in_handler = false;
				if (timeout._in_discard_blockade)
					timeout._discard_blockade.wakeup();

			});
	}

	/* schedule soonest alarm at time source */
	_alarms.soonest(Clock { 0 }).with_result(
		[&] (Clock soonest) {
			_schedule_alarm(soonest);
		},
		[&] (Alarms::None) {
			/* FIXME only needed for periodic real-time update */
			_schedule_alarm(Clock { Clock::MASK });
		}
	);

	_mutex.release();
}


Timeout_scheduler::Timeout_scheduler(Time_source  &time_source,
                                     Microseconds  accuracy_us)
: _time_source { time_source }, _accuracy_us(accuracy_us)
{ }


Timeout_scheduler::~Timeout_scheduler()
{
	Mutex::Guard scheduler_guard { _mutex };

	/* clear alarm registry */
	while (_alarms.with_any_in_range(Clock { 0 }, Clock { Clock::MASK },
		[&] (Alarm &alarm) {
			_discard_timeout_unsynchronized(alarm.timeout);
		}));
}


void Timeout_scheduler::_schedule_alarm(Clock time)
{
	_alarm_time.construct(time);
	_time_source.set_alarm(Duration { Microseconds { _alarm_time->value() } });
}


void Timeout_scheduler::_schedule_timeout(Timeout            &timeout,
                                          Microseconds const  duration,
                                          Microseconds const  period)
{
	/* acquire scheduler mutex */
	Mutex::Guard const scheduler_guard { _mutex };

	timeout._period = period;

	Clock const now { _time_source.curr_time() };

	Clock time { now };
	time.add(duration);
	timeout._alarm.construct(_alarms, timeout, time);

	if (_alarm_time.constructed()) {
		/*
		 * Omit setting the time-source alarm, if we are currently
		 * handling timeouts. This is the case when _alarm_time
		 * is in the past.
		 */
		if (_alarm_time->earlier(now))
			return;
		
		/*
		 * If the new alarm is close enough to the _alarm_time,
		 * we do not update the time-source alarm.
		 */
		time.add(_accuracy_us);
		if (!time.earlier(*_alarm_time))
			return;
	}

	_schedule_alarm(timeout._alarm->time);
}


void Timeout_scheduler::_discard_timeout_unsynchronized(Timeout &timeout)
{
	if (timeout._in_handler) {

		if (timeout._in_discard_blockade)
			error("timeout is getting discarded by multiple threads");

		/*
		 * We cannot discard a timeout whose handler is currently executed. We
		 * rather set its flag '_in_discard_blockade' (this ensures that the
		 * timeout handler is not getting called again) and then wait for the
		 * current handler call to finish. 'Timeout_scheduler::handle_timeout'
		 * will wake us up as soon as the handler returned.
		 */
		timeout._in_discard_blockade = true;
		_mutex.release();

		timeout._discard_blockade.block();

		_mutex.acquire();
		timeout._in_discard_blockade = false;
	}

	timeout._alarm.destruct();

	/*
	 * Keep the current time-source alarm if the soonest deadline is close
	 * enough.
	 */
	_alarms.soonest(Clock { 0 }).with_result(
		[&] (Clock soonest) {
			Clock deadline { *_alarm_time };
			deadline.add(_accuracy_us);

			if (deadline.earlier(soonest))
				_schedule_alarm(soonest);
		},
		[&] (Alarms::None) {
			_schedule_alarm(Clock { Clock::MASK });
		}
	);
}
