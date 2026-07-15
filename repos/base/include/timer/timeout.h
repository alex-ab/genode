/*
 * \brief  Multiplexing one time source amongst different timeouts
 * \author Martin Stein
 * \date   2016-11-04
 *
 * These classes are not meant to be used directly. They merely exist to share
 * the generic parts of timeout-scheduling between the Timer::Connection and the
 * Timer driver. For user-level timeout-scheduling you should use the interface
 * in timer_session/connection.h instead.
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _TIMER__TIMEOUT_H_
#define _TIMER__TIMEOUT_H_

/* Genode includes */
#include <util/noncopyable.h>
#include <base/duration.h>
#include <base/mutex.h>
#include <util/misc_math.h>
#include <util/alarm_registry.h>

namespace Genode {

	class Time_source;
	class Timeout;
	class Timeout_handler;
	class Timeout_scheduler;
}

namespace Timer {

	class Connection;
}


/**
 * Interface of a timeout callback
 */
struct Genode::Timeout_handler : Interface
{
	virtual void handle_timeout(Duration curr_time) = 0;
};


/**
 * Interface of a time source that can handle one timeout at a time
 */
struct Genode::Time_source : Interface
{
	/**
	 * Return the current time of the source
	 */
	virtual Duration curr_time() = 0;

	/**
	 * Install an alarm, overrides the last timeout if any
	 *
	 * \param deadline    absolute alarm time
	 */
	virtual void set_alarm(Duration deadline) = 0;
};


/**
 * Timeout callback that can be used for both one-shot and periodic timeouts
 *
 * This class should be used only if it is necessary to use one timeout
 * callback for both periodic and one-shot timeouts. This is the case, for
 * example, in a Timer-session server. If this is not the case, the classes
 * Periodic_io_timeout and One_shot_io_timeout are the better choice.
 */
class Genode::Timeout : private Noncopyable
{
	friend class Timeout_scheduler;

	private:

		struct Clock;
		struct Alarm;
		using  Alarms = Alarm_registry<Alarm, Clock>;

		struct Clock
		{
			using Duration     = Genode::Duration;
			using Microseconds = Genode::Microseconds;

			uint64_t _us { 0 };

			static constexpr uint64_t MASK = ~0ULL;

			Clock() = default;

			Clock(uint64_t us)            { add(Microseconds { us }); }

			Clock(Duration duration)      { add(duration.trunc_to_plain_us()); }

			inline
			void add(Microseconds us)     { _us += min(us.value, MASK-_us); }
			
			uint64_t value()        const { return _us; }

			bool earlier(Clock rhs) const { return _us < rhs._us; }

			void print(Output &out) const { Microseconds { _us }.print(out); }
		};


		struct Alarm : Alarms::Element
		{
			Timeout &timeout;

			Alarm(Alarms &alarms, Timeout &timeout, Clock time)
			:
			  Alarms::Element(alarms, *this, time), timeout(timeout)
			{ }
		};

		Timeout_scheduler     &_scheduler;
		Microseconds           _period              { 0 };
		Timeout_handler       &_handler;

		/* modified by Timeout_scheduler (requires mutex-ing the registry) */
		Constructible<Alarm>   _alarm               { };

		Timeout(Timeout const &);

		Timeout &operator = (Timeout const &);

	public:

		Timeout(Timeout_scheduler &scheduler, Timeout_handler &handler);

		Timeout(Timer::Connection &timer_connection, Timeout_handler &handler);

		~Timeout();

		void schedule_periodic(Microseconds duration);

		void schedule_one_shot(Microseconds duration);

		void discard();

		bool scheduled();

		Duration deadline() const;
};


/**
 * Multiplexes one time source amongst different timeouts
 */
class Genode::Timeout_scheduler : private Noncopyable,
                                  public  Timeout_handler
{
	friend class Timeout;

	private:

		using Clock  = Timeout::Clock;
		using Alarm  = Timeout::Alarm;
		using Alarms = Timeout::Alarms;

		Mutex                _handle_mutex       { };
		Mutex                _schedule_mutex     { };
		Time_source         &_time_source;
		Alarms               _alarms             { };
		Microseconds const   _accuracy_us;
		Constructible<Clock> _alarm_time         { };

		void _discard_timeout_unsynchronized(Timeout &timeout);

		void _schedule_alarm(Clock time);

		void _schedule_timeout(Timeout      &timeout,
		                       Microseconds  duration,
		                       Microseconds  period);

		Timeout_scheduler(Timeout_scheduler const &);

		Timeout_scheduler &operator = (Timeout_scheduler const &);

	public:

		Timeout_scheduler(Time_source  &time_source, Microseconds accuracy_us);

		~Timeout_scheduler();

		/*********************
		 ** Timeout_handler **
		 *********************/

		void handle_timeout(Duration curr_time) override;

};

#endif /* _TIMER__TIMEOUT_H_ */
