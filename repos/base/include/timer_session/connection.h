/*
 * \brief  Connection to timer service and timeout scheduler
 * \author Norman Feske
 * \date   2008-08-22
 */

/*
 * Copyright (C) 2008-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__TIMER_SESSION__CONNECTION_H_
#define _INCLUDE__TIMER_SESSION__CONNECTION_H_

/* Genode includes */
#include <timer_session/client.h>
#include <base/connection.h>
#include <util/reconstructible.h>
#include <base/entrypoint.h>
#include <timer/timeout.h>
#include <trace/timestamp.h>

namespace Timer
{
	class Connection;
	template <typename> class Periodic_io_timeout;
	template <typename> class One_shot_io_timeout;
	template <typename> class Periodic_timeout;
	template <typename> class One_shot_timeout;
}


/**
 * I/O-level periodic timeout that is linked to a custom handler, scheduled when constructed
 */
template <typename HANDLER>
struct Timer::Periodic_io_timeout : private Genode::Noncopyable,
                                    private Genode::Timeout_handler
{
	private:

		using Duration          = Genode::Duration;
		using Timeout           = Genode::Timeout;
		using Microseconds      = Genode::Microseconds;

		typedef void (HANDLER::*Handler_method)(Duration);

		Timeout               _timeout;
		HANDLER              &_object;
		Handler_method const  _method;


		/*********************
		 ** Timeout_handler **
		 *********************/

		void handle_timeout(Duration curr_time) override {
			(_object.*_method)(curr_time); }

	public:

		Periodic_io_timeout(Connection     &timer,
		                    HANDLER        &object,
		                    Handler_method  method,
		                    Microseconds    duration)
		:
			_timeout { timer },
			_object  { object },
			_method  { method }
		{
			_timeout.schedule_periodic(duration, *this);
		}
};


/**
 * I/O-level one-shot timeout that is linked to a custom handler, scheduled manually
 */
template <typename HANDLER>
class Timer::One_shot_io_timeout : private Genode::Noncopyable,
                                   private Genode::Timeout_handler
{
	private:

		using Duration          = Genode::Duration;
		using Timeout           = Genode::Timeout;
		using Microseconds      = Genode::Microseconds;

		typedef void (HANDLER::*Handler_method)(Duration);

		Timeout               _timeout;
		HANDLER              &_object;
		Handler_method const  _method;


		/*********************
		 ** Timeout_handler **
		 *********************/

		void handle_timeout(Duration curr_time) override {
			(_object.*_method)(curr_time); }

	public:

		One_shot_io_timeout(Connection     &timer,
		                    HANDLER        &object,
		                    Handler_method  method)
		:
			_timeout { timer },
			_object  { object },
			_method  { method }
		{ }

		void schedule(Microseconds duration) {
			_timeout.schedule_one_shot(duration, *this); }

		void discard() { _timeout.discard(); }

		bool scheduled() { return _timeout.scheduled(); }

		Microseconds deadline() const { return _timeout.deadline(); }
};


/**
 * Application-level periodic timeout that is linked to a custom handler, scheduled when constructed
 */
template <typename HANDLER>
class Timer::Periodic_timeout : private Genode::Noncopyable
{
	private:

		using Duration          = Genode::Duration;
		using Io_timeout        = Timer::Periodic_io_timeout<Periodic_timeout>;
		using Microseconds      = Genode::Microseconds;
		using Signal_handler    = Genode::Signal_handler<Periodic_timeout>;
		using Mutex             = Genode::Mutex;

		typedef void (HANDLER::*Handler_method)(Duration);

		Mutex                 _mutex { };
		bool                  _in_destruct { false };
		HANDLER              &_object;
		Handler_method const  _method;
		Signal_handler        _timeout_handler;
		Io_timeout            _io_timeout;

		Duration              _curr_time { Microseconds { 0 } };

		void _handle_io_timeout(Duration curr_time)
		{
			_curr_time = curr_time;
			_timeout_handler.local_submit();
		}

		void _handle_timeout()
		{
			Mutex::Guard guard(_mutex);
			if (!_in_destruct)
				(_object.*_method)(_curr_time);
		}

	public:

		Periodic_timeout(Connection     &timer,
		                 HANDLER        &object,
		                 Handler_method  method,
		                 Microseconds    duration);

		~Periodic_timeout()
		{
			Mutex::Guard guard(_mutex);
			_in_destruct = true;
		}
};


/**
 * Application-level one-shot timeout that is linked to a custom handler
 */
template <typename HANDLER>
class Timer::One_shot_timeout : private Genode::Noncopyable
{
	private:

		using Duration          = Genode::Duration;
		using Io_timeout        = Timer::One_shot_io_timeout<One_shot_timeout>;
		using Microseconds      = Genode::Microseconds;
		using Signal_handler    = Genode::Signal_handler<One_shot_timeout>;
		using Mutex             = Genode::Mutex;

		typedef void (HANDLER::*Handler_method)(Duration);

		Mutex                 _handle_mutex   { };
		Mutex                 _schedule_mutex { };
		bool                  _in_discard { false };
		HANDLER              &_object;
		Handler_method const  _method;
		Signal_handler        _timeout_handler;
		Io_timeout            _io_timeout;

		Duration              _curr_time { Microseconds { 0 } };

		void _handle_io_timeout(Duration curr_time)
		{
			_curr_time = curr_time;
			_timeout_handler.local_submit();
		}

		void _handle_timeout()
		{
			Mutex::Guard guard(_handle_mutex);
			if (!_in_discard)
				(_object.*_method)(_curr_time);
		}

	public:

		One_shot_timeout(Connection     &timer,
		                 HANDLER        &object,
		                 Handler_method  method);

		~One_shot_timeout()
		{
			Mutex::Guard guard(_handle_mutex);
			_in_discard = true;
		}

		void discard()
		{
			Mutex::Guard   handle_guard(_handle_mutex);
			Mutex::Guard schedule_guard(_schedule_mutex);
			_in_discard = true;
			_io_timeout.discard();
		}

		void schedule(Microseconds duration)
		{
			Mutex::Guard schedule_guard(_schedule_mutex);
			_in_discard = false;
			_io_timeout.schedule(duration);
		}

		bool scheduled()                     { return _io_timeout.scheduled(); }

		Microseconds deadline() const        { return _io_timeout.deadline(); }
};


/**
 * Connection to timer service and timeout scheduler
 *
 * Multiplexes a timer session amongst different timeouts.
 */
class Timer::Connection : public  Genode::Connection<Session>,
                          public  Session_client,
                          private Genode::Time_source
{
	friend class Genode::Timeout;
	template <typename> friend class One_shot_timeout;
	template <typename> friend class Periodic_timeout;

	private:

		using Timeout           = Genode::Timeout;
		using Timeout_handler   = Genode::Timeout_handler;
		using Timestamp         = Genode::Trace::Timestamp;
		using Duration          = Genode::Duration;
		using Mutex             = Genode::Mutex;
		using Microseconds      = Genode::Microseconds;
		using Milliseconds      = Genode::Milliseconds;
		using Entrypoint        = Genode::Entrypoint;
		using Io_signal_handler = Genode::Io_signal_handler<Connection>;

		/*
		 * Noncopyable
		 */
		Connection(Connection const &);
		Connection &operator = (Connection const &);

		/*
		 * The mode determines which interface of the timer connection is
		 * enabled. Initially, a timer connection is in TIMER_SESSION mode.
		 * In this mode, the user can operate directly on the connection using
		 * the methods of the timer-session interface. As soon as the
		 * connection is handed over as argument to the constructor of a
		 * Periodic_io_timeout or a One_shot_io_timeout, it switches to
		 * TIMEOUT_FRAMEWORK mode. From this point on, the only method that
		 * the user can use directly on the connection is 'curr_time'. For
		 * the rest of the functionality he rather uses the interfaces of
		 * timeout objects that reference the connection.
		 *
		 * These are the characteristics of the two modes:
		 *
		 *    TIMER_SESSION:
		 *
		 *       * Allows for both blocking and non-blocking timeout semantics.
		 *       * Missing local interpolation leads to less precise curr_time
		 *         results.
		 *       * Only one timeout at a time per connection.
		 *
		 *    TIMEOUT FRAMEWORK:
		 *
		 *       * Supports only non-blocking timeout semantics.
		 *       * More precise curr_time results through local interpolation.
		 *       * Multiplexing of multiple timeouts at the same connection
		 */
		enum Mode { TIMER_SESSION, TIMEOUT_FRAMEWORK };

		Mode                    _mode             { TIMER_SESSION };
		Mutex                   _mutex            { };
		Genode::Signal_receiver _sig_rec          { };
		Genode::Signal_context  _default_sigh_ctx { };

		Genode::Signal_context_capability
			_default_sigh_cap = _sig_rec.manage(_default_sigh_ctx);

		Genode::Signal_context_capability _custom_sigh_cap { };

		void _sigh(Signal_context_capability sigh)
		{
			Session_client::sigh(sigh);
		}

		/****************************************************
		 ** Members for interaction with Timeout framework **
		 ****************************************************/

		enum { MIN_TIMEOUT_US             = 1000 };
		enum { REAL_TIME_UPDATE_PERIOD_US = 500000 };
		enum { MAX_INTERPOLATION_QUALITY  = 3 };
		enum { MAX_REMOTE_TIME_LATENCY_US = 500 };
		enum { MAX_REMOTE_TIME_TRIALS     = 5 };
		enum { NR_OF_INITIAL_CALIBRATIONS = 3 * MAX_INTERPOLATION_QUALITY };
		enum { MIN_FACTOR_LOG2            = 8 };
		enum { MAX_DRIFT_US               = 1000 };

		Entrypoint               &_ep;
		Io_signal_handler         _signal_handler        { _ep, *this, &Connection::_handle_timeout };
		Timeout_handler          *_handler               { nullptr };
		Mutex                     _real_time_mutex       { };
		uint64_t                  _us                    { elapsed_us() };
		Timestamp                 _ts                    { _timestamp() };
		Duration                  _real_time             { Microseconds { _us } };
		Duration                  _interpolated_time     { _real_time };
		unsigned                  _interpolation_quality { 0 };
		uint64_t                  _us_to_ts_factor       { 1 };
		unsigned                  _us_to_ts_factor_shift { 0 };
		Genode::Timeout_scheduler _timeout_scheduler     { *this, Microseconds { 1 } };

		Genode::Timeout_scheduler &_switch_to_timeout_framework_mode();

		Timestamp _timestamp();

		void _update_interpolation_quality(uint64_t min_factor,
		                                   uint64_t max_factor);

		uint64_t _ts_to_us_ratio(Timestamp ts,
		                         uint64_t  us,
		                         unsigned  shift);

		void _update_real_time();

		Duration _update_interpolated_time(Duration &interpolated_time);

		void _handle_timeout();


		/*****************
		 ** Time_source **
		 *****************/

		void set_timeout(Microseconds duration, Timeout_handler &handler) override;
		Microseconds max_timeout() const override { return Microseconds(REAL_TIME_UPDATE_PERIOD_US); }

	public:

		/**
		 * Constructor
		 *
		 * \param env    environment used for construction (e.g. quota trading)
		 * \param ep     entrypoint used as timeout handler execution context
		 * \param label  optional label used in session routing
		 */
		Connection(Genode::Env &env,
		           Genode::Entrypoint &ep,
		           Label const &label = Label());

		/**
		 * Convenience constructor wrapper using the environment's entrypoint as
		 * timeout handler execution context
		 */
		Connection(Genode::Env &env, Label const &label = Label())
		: Connection(env, env.ep(), label) { }

		~Connection() { _sig_rec.dissolve(_default_sigh_ctx); }

		/*
		 * Intercept 'sigh' to keep track of customized signal handlers
		 */
		void sigh(Signal_context_capability sigh) override
		{
			if (_mode == TIMEOUT_FRAMEWORK) {
				Genode::error("unable to register timer signal handler");
				return;
			}
			_custom_sigh_cap = sigh;
			Session_client::sigh(_custom_sigh_cap);
		}

		/*
		 * Block for a time span of 'us' microseconds
		 */
		void usleep(uint64_t us) override
		{
			if (_mode == TIMEOUT_FRAMEWORK) {
				Genode::error("attempt to usleep in timeout-framework mode");
				return;
			}
			/*
			 * Omit the interaction with the timer driver for the corner case
			 * of not sleeping at all. This corner case may be triggered when
			 * polling is combined with sleeping (as some device drivers do).
			 * If we passed the sleep operation to the timer driver, the
			 * timer would apply its policy about a minimum sleep time to
			 * the sleep operation, which is not desired when polling.
			 */
			if (us == 0)
				return;

			/* serialize sleep calls issued by different threads */
			Mutex::Guard guard(_mutex);

			/* temporarily install to the default signal handler */
			if (_custom_sigh_cap.valid())
				Session_client::sigh(_default_sigh_cap);

			/* trigger timeout at default signal handler */
			trigger_once(us);
			_sig_rec.wait_for_signal();

			/* revert custom signal handler if registered */
			if (_custom_sigh_cap.valid())
				Session_client::sigh(_custom_sigh_cap);
		}

		/*
		 * Block for a time span of 'ms' milliseconds
		 */
		void msleep(uint64_t ms) override
		{
			if (_mode == TIMEOUT_FRAMEWORK) {
				Genode::error("attempt to msleep in timeout-framework mode");
				return;
			}
			usleep(1000*ms);
		}


		/*****************
		 ** Time_source **
		 *****************/

		Duration curr_time() override;
};


template <typename T>
Timer::Periodic_timeout<T>::Periodic_timeout(
	Timer::Connection                   &timer,
	T                                   &object,
	Periodic_timeout<T>::Handler_method  method,
	Genode::Microseconds                 duration)
:
	_object          { object },
	_method          { method },
	_timeout_handler { timer._ep, *this, &Periodic_timeout::_handle_timeout },
	_io_timeout      { timer, *this, &Periodic_timeout::_handle_io_timeout, duration }
{ }


template <typename T>
Timer::One_shot_timeout<T>::One_shot_timeout(
	Timer::Connection                   &timer,
	T                                   &object,
	One_shot_timeout<T>::Handler_method  method)
:
	_object          { object },
	_method          { method },
	_timeout_handler { timer._ep, *this, &One_shot_timeout::_handle_timeout },
	_io_timeout      { timer, *this, &One_shot_timeout::_handle_io_timeout }
{ }

#endif /* _INCLUDE__TIMER_SESSION__CONNECTION_H_ */
