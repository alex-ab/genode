/*
 * \brief  Timer driver for Linux
 * \author Norman Feske
 * \author Alexander Boettcher
 * \date   2024-06-18
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
#include <base/attached_rom_dataspace.h>
#include <timer/component.h>

/* Linux includes */
#include <linux_syscalls.h>
#include <sys/time.h>


namespace Timer {

	using namespace Genode;

	struct Linux_device;
	struct Main;
}


class Timer::Linux_device : public Timer::Device
{
	public:

		struct Wakeup_dispatcher : Interface
		{
			virtual void dispatch_device_wakeup() = 0;
		};

	private:

		struct Waiter : Thread
		{
			Wakeup_dispatcher &_dispatcher;

			Mutex    _mutex    { }; /* protect '_deadline' */
			Deadline _deadline { ~0ULL };

			Device  &_device;

			Waiter(Env &env, Wakeup_dispatcher &dispatcher, Device &device)
			:
				Thread(env, "waiter", Stack_size { 64*1024 }),
				_dispatcher(dispatcher),
				_device(device)
			{
				start();
			}

			void entry() override
			{
				for (;;) {

					auto deadline_atomic = [&]
					{
						Mutex::Guard guard(_mutex);
						return _deadline;
					};

					{
						auto const deadline = deadline_atomic();
						auto const now      = _device.now();

						if (now.us < deadline.us) {
							/* no support to cancel sleep, use 1ms granularity */
							auto usecs = min(deadline.us - now.us, 1000ull);

							struct timespec ts {
								.tv_sec  =  long(usecs) / (1000 * 1000),
								.tv_nsec = (long(usecs) % (1000 * 1000)) * 1000,
							};

							lx_nanosleep(&ts, &ts);
						}
					}

					if (_device.now().us >= deadline_atomic().us)
						_dispatcher.dispatch_device_wakeup();
				}
			}

			void update_deadline(Deadline const deadline)
			{
				Mutex::Guard guard(_mutex);

				bool const sooner_than_scheduled = (deadline.us < _deadline.us);

				_deadline = deadline;

				if (sooner_than_scheduled) {
					/* cancel old timeout by waking sleeping waiter */

					/* XXX not supported to cancel nanosleep */
				}
			}
		} _waiter;

		int lx_gettimeofday(struct timeval *tv, struct timeval *tz) const {
			return int(lx_syscall(SYS_gettimeofday, tv, tz)); }

	public:

		Linux_device(Env &env, Wakeup_dispatcher &dispatcher)
		: _waiter(env, dispatcher, *this) { }

		Clock now() override
		{
			struct timeval tv { };

			lx_gettimeofday(&tv, 0);

			return { .us = uint64_t(tv.tv_sec) * 1000 * 1000 + tv.tv_usec };
		}

		bool update_deadline(Deadline deadline) override
		{
			_waiter.update_deadline(deadline);
			return true;
		}
};


struct Timer::Main : Linux_device::Wakeup_dispatcher
{
	Env &_env;

	Linux_device _device { _env, *this };

	Mutex  _alarms_mutex { };
	Alarms _alarms { };

	Sliced_heap _sliced_heap { _env.ram(), _env.rm() };

	Root _root { _env, _sliced_heap, _alarms, _alarms_mutex, _device };

	/**
	 * Device::Wakeup_dispatcher
	 */
	void dispatch_device_wakeup() override
	{
		Mutex::Guard guard(_alarms_mutex);

		/* handle and remove pending alarms */
		while (_alarms.with_any_in_range({ 0 }, _device.now(), [&] (Alarm &alarm) {
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
