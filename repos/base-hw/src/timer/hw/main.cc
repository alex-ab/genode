/*
 * \brief  Timer driver for the base-hw kernel
 * \author Norman Feske
 * \date   2024-03-11
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>
#include <base/heap.h>
#include <trace/timestamp.h>
#include <timer/component.h>

/* base-hw includes */
#include <kernel/interface.h>

namespace Timer {

	using namespace Genode;

	struct Hw_device;
	struct Main;
}


class Timer::Hw_device : Noncopyable, public Timer::Device
{
	public:

		struct Wakeup_dispatcher : Interface
		{
			virtual void dispatch_device_wakeup() = 0;
		};

	private:

		Env &_env;

		Kernel::time_t const _max_timeout_us = Kernel::timeout_max_us();

		Wakeup_dispatcher &_dispatcher;

		Signal_handler<Hw_device> _handler { _env.ep(), *this, &Hw_device::_handle_timeout };

		Signal_context_capability const _handler_cap = _handler;

		Kernel::capid_t const _sel = Kernel::capid_t(addr_t(_handler_cap.data()) & 0xffffu);

		void _handle_timeout() { _dispatcher.dispatch_device_wakeup(); }

	public:

		Hw_device(Env &env, Wakeup_dispatcher &dispatcher)
		: _env(env), _dispatcher(dispatcher) { }

		Clock now() override
		{
			return Clock { .us = Kernel::time() };
		}

		bool update_deadline(Deadline const deadline) override
		{
			uint64_t const now_us = now().us;
			uint64_t const rel_us = (deadline.us > now_us)
			                      ? min(_max_timeout_us, deadline.us - now_us)
			                      : 0;

			Kernel::timeout(Kernel::timeout_t(rel_us), _sel);

			return true;
		}
};


struct Timer::Main : Hw_device::Wakeup_dispatcher
{
	Env &_env;

	Hw_device _device { _env, *this };

	Mutex  _alarms_mutex { };
	Alarms _alarms { };

	Sliced_heap _sliced_heap { _env.ram(), _env.rm() };

	Root _root { _env, _sliced_heap, _alarms, _alarms_mutex, _device };

	/**
	 * Device::Wakeup_dispatcher
	 */
	void dispatch_device_wakeup() override
	{
		Clock const now = _device.now();

		/* handle and remove pending alarms */
		while (_alarms.with_any_in_range({ 0 }, now, [&] (Alarm &alarm) {
			alarm.session.handle_wakeup(); }));

		/* schedule next wakeup */
		_device.update_deadline(next_deadline(_alarms));
	}

	Main(Genode::Env &env) : _env(env)
	{
		_env.parent().announce(_env.ep().manage(_root));
	}
};


void Component::construct(Genode::Env &env) { static Timer::Main inst(env); }
