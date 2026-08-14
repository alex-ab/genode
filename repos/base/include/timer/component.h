/*
 * \brief  Platform-independent part of timer component
 * \author Johannes Schlatow
 * \date   2026-08-14
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <root/component.h>
#include <base/session_object.h>
#include <timer_session/timer_session.h>
#include <util/alarm_registry.h>

namespace Timer {

	using namespace Genode;

	struct Clock;
	struct Device;
	struct Alarm;
	struct Root;
	struct Session_component;

	using Alarms = Alarm_registry<Alarm, Clock>;
}


struct Timer::Clock
{
	uint64_t us;

	static constexpr uint64_t MASK = uint64_t(-1);

	uint64_t value() const { return us; }

	void print(Output &out) const { Genode::print(out, us); }
};


struct Timer::Device : Interface
{
	struct Deadline : Clock { };
	static constexpr Deadline infinite_deadline { uint64_t(-1) };

	virtual Clock now() = 0;
	virtual bool update_deadline(Deadline deadline) = 0;
	virtual void notify() { };
};


struct Timer::Alarm : Alarms::Element
{
	Session_component &session;

	Alarm(Alarms &alarms, Session_component &session, Clock time)
	:
		Alarms::Element(alarms, *this, time), session(session)
	{ }

	void print(Output &out) const;
};


static Timer::Device::Deadline next_deadline(Timer::Alarms &alarms)
{
	using namespace Timer;

	return alarms.soonest(Clock { 0 }).convert<Device::Deadline>(
		[&] (Clock soonest) -> Device::Deadline {

			/* scan alarms for a cluster nearby the soonest */
			uint64_t const MAX_DELAY_US = 250;
			Device::Deadline result { soonest.us };
			alarms.for_each_in_range(soonest, Clock { soonest.us + MAX_DELAY_US },
			[&] (Alarm const &alarm) {
				result.us = max(result.us, alarm.time.us); });

			return result;
		},
		[&] (Alarms::None) { return Device::infinite_deadline; });
}


struct Timer::Session_component : Session_object<Timer::Session, Session_component>
{
	Alarms &_alarms;
	Mutex  &_alarms_mutex;
	Device &_device;

	Signal_context_capability _sigh { };

	Clock const _creation_time = _device.now();

	uint64_t _local_now_us() const { return _device.now().us - _creation_time.us; }

	struct Period { uint64_t us; };

	Constructible<Period> _period { };
	Constructible<Alarm>  _alarm  { };

	Session_component(Env             &env,
	                  Resources const &resources,
	                  Label     const &label,
	                  Alarms          &alarms,
	                  Mutex           &alarms_mutex,
	                  Device          &device)
	:
		Session_object(env.ep(), resources, label),
		_alarms(alarms), _alarms_mutex(alarms_mutex), _device(device)
	{ }

	~Session_component()
	{
		Mutex::Guard guard(_alarms_mutex);

		_alarm.destruct();
	}

	/**
	 * Called by Device::Wakeup_dispatcher with '_alarms_mutex' taken
	 */
	void handle_wakeup()
	{
		if (_sigh.valid())
			Signal_transmitter(_sigh).submit();

		if (_period.constructed()) {
			Clock const next = _alarm.constructed()
			                 ? Clock { _alarm->time.us  + _period->us }
			                 : Clock { _device.now().us + _period->us };

			_alarm.construct(_alarms, *this, next);

		} else /* response of 'trigger_once' */ {
			_alarm.destruct();
		}
	}

	/******************************
	 ** Timer::Session interface **
	 ******************************/

	void trigger_once(uint64_t rel_us) override
	{
		Mutex::Guard guard(_alarms_mutex);

		_period.destruct();
		_alarm.destruct();

		Clock const now = _device.now();

		rel_us = max(rel_us, 250u);
		_alarm.construct(_alarms, *this, Clock { now.us + rel_us });

		if (!_device.update_deadline(next_deadline(_alarms)))
			_device.notify();
	}

	void trigger_periodic(uint64_t period_us) override
	{
		Mutex::Guard guard(_alarms_mutex);

		_period.destruct();
		_alarm.destruct();

		if (period_us) {
			period_us = max(period_us, 1000u);
			_period.construct(period_us);
			handle_wakeup();
		}

		if (!_device.update_deadline(next_deadline(_alarms)))
			_device.notify();
	}

	uint64_t trigger_at(uint64_t abs_us) override
	{
		Mutex::Guard guard(_alarms_mutex);

		_period.destruct();
		_alarm.destruct();

		Clock const now = _device.now();

		abs_us = max(abs_us + _creation_time.us, now.us + 250u);
		_alarm.construct(_alarms, *this, Clock { abs_us });

		if (!_device.update_deadline(next_deadline(_alarms)))
			_device.notify();

		return now.us;
	}

	void sigh(Signal_context_capability sigh) override { _sigh = sigh; }

	uint64_t elapsed_ms() const override { return _local_now_us()/1000; }
	uint64_t elapsed_us() const override { return _local_now_us(); }

	void msleep(uint64_t) override { }
	void usleep(uint64_t) override { }
};


struct Timer::Root : public Root_component<Session_component>
{
	private:

		Env    &_env;
		Alarms &_alarms;
		Mutex  &_alarms_mutex;
		Device &_device;

	protected:

		Create_result _create_session(const char *args) override
		{
			return *new (md_alloc())
				Session_component(_env,
				                  session_resources_from_args(args),
				                  session_label_from_args(args),
				                  _alarms, _alarms_mutex, _device);
		}

		void _upgrade_session(Session_component &s, const char *args) override
		{
			s.upgrade(ram_quota_from_args(args));
			s.upgrade(cap_quota_from_args(args));
		}

		void _destroy_session(Session_component &s) override
		{
			Genode::destroy(md_alloc(), &s);
		}

	public:

		Root(Env &env, Allocator &md_alloc,
		     Alarms &alarms, Mutex &alarms_mutex, Device &device)
		:
			Root_component<Session_component>(&env.ep().rpc_ep(), &md_alloc),
			_env(env), _alarms(alarms), _alarms_mutex(alarms_mutex), _device(device)
		{ }
};


void Timer::Alarm::print(Output &out) const { Genode::print(out, session.label()); }
