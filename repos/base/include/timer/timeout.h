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
#include <util/list.h>
#include <base/duration.h>
#include <base/mutex.h>
#include <util/misc_math.h>
#include <base/blockade.h>

namespace Genode {

	class Time_source;
	class Timeout;
	class Timeout_handler;
	class Timeout_scheduler;
}

namespace Timer {

	class Connection;
	class Root_component;
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
class Genode::Timeout : private Noncopyable,
                        public Genode::List<Timeout>::Element
{
	friend class Timeout_scheduler;

	private:

		Mutex                  _mutex               { };
		Timeout_scheduler     &_scheduler;
		Microseconds           _period              { 0 };
		Microseconds           _deadline            { Microseconds { 0 } };
		List_element<Timeout>  _pending_timeouts_le { this };
		Timeout_handler       *_pending_handler     { nullptr };
		Timeout_handler       &_handler;
		bool                   _scheduled           { false };
		bool                   _in_discard_blockade { false };
		Blockade               _discard_blockade    { };

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

		Microseconds deadline() const { return _deadline; }
};


/**
 * Multiplexes one time source amongst different timeouts
 */
class Genode::Timeout_scheduler : private Noncopyable,
                                  public  Timeout_handler
{
	friend class Timer::Connection;
	friend class Timer::Root_component;
	friend class Timeout;

	private:

		Mutex               _mutex              { };
		Time_source        &_time_source;
		List<Timeout>       _timeouts           { };
		bool                _destructor_called  { false };

		void _insert_into_timeouts_list(Timeout &timeout);

		void _schedule_timeout(Timeout      &timeout,
		                       Microseconds  duration,
		                       Microseconds  period);

		void _discard_timeout_unsynchronized(Timeout &timeout);

		void _schedule_one_shot_timeout(Timeout      &timeout,
		                                Microseconds  duration);

		void _schedule_periodic_timeout(Timeout      &timeout,
		                                Microseconds  period);

		void _discard_timeout(Timeout &timeout);

		void _destruct_timeout(Timeout &timeout);

		Timeout_scheduler(Timeout_scheduler const &);

		Timeout_scheduler &operator = (Timeout_scheduler const &);


		/*********************
		 ** Timeout_handler **
		 *********************/

		void handle_timeout(Duration curr_time) override;

	public:

		Timeout_scheduler(Time_source  &time_source);

		~Timeout_scheduler();

		Duration curr_time();
};

#endif /* _TIMER__TIMEOUT_H_ */
