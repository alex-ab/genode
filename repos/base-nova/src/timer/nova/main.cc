/*
 * \brief  Timer driver for NOVA
 * \author Norman Feske
 * \date   2024-03-07
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

/* NOVA includes */
#include <nova/native_thread.h>

namespace Timer {

	using namespace Genode;

	struct Tsc { uint64_t tsc; };
	struct Tsc_rate;
	struct Nova_device;
	struct Main;
}


struct Timer::Tsc_rate
{
	unsigned long khz;

	static Tsc_rate from_node(Node const &node)
	{
		unsigned long khz = 0;
		node.with_optional_sub_node("hardware", [&] (Node const &hardware) {
			hardware.with_optional_sub_node("tsc", [&] (Node const &tsc) {
				khz = tsc.attribute_value("freq_khz", 0UL); }); });
		return { khz };
	}

	Tsc tsc_from_clock(Clock clock) const
	{
		return { .tsc = (clock.us*khz)/1000 };
	}

	Clock clock_from_tsc(Tsc tsc) const
	{
		return { .us = (khz > 0) ? (tsc.tsc*1000)/khz : 0 };
	}
};


class Timer::Nova_device : public Timer::Device
{
	public:

		struct Wakeup_dispatcher : Interface
		{
			virtual void dispatch_device_wakeup() = 0;
		};

	private:

		Tsc_rate const _tsc_rate;

		struct Waiter : Thread
		{
			struct Sel /* NOVA kernel-capability selector */
			{
				addr_t value;

				static Sel init_signal_sem(Thread &thread)
				{
					addr_t exc_base = Native_thread::INVALID_INDEX;
					thread.with_native_thread([&] (Native_thread &nt) {
						exc_base = nt.exc_pt_sel; });

					request_signal_sm_cap(exc_base + Nova::PT_SEL_PAGE_FAULT,
					                      exc_base + Nova::SM_SEL_SIGNAL);

					return { exc_base + Nova::SM_SEL_SIGNAL };
				}

				auto down(Tsc deadline)
				{
					return Nova::sm_ctrl(value, Nova::SEMAPHORE_DOWN, deadline.tsc);
				}

				auto up()
				{
					return Nova::sm_ctrl(value, Nova::SEMAPHORE_UP);
				}
			};

			Wakeup_dispatcher &_dispatcher;

			Sel _wakeup_sem { }; /* must be initialize by waiter thread */

			Mutex _mutex    { }; /* protect '_deadline' */
			Tsc   _deadline { uint64_t(-1) };

			Waiter(Env &env, Wakeup_dispatcher &dispatcher)
			:
				Thread(env, "waiter", Stack_size { 64*1024 }),
				_dispatcher(dispatcher)
			{
				start();
			}

			void entry() override
			{
				_wakeup_sem = Sel::init_signal_sem(*this);

				for (;;) {

					auto deadline_tsc = [&]
					{
						Mutex::Guard guard(_mutex);
						return _deadline;
					};

					/*
					 * Block until timeout fires or it gets canceled.
					 * When triggered (not canceled by 'update_deadline'),
					 * call 'dispatch_device_wakeup'.
					 */
					if (_wakeup_sem.down(deadline_tsc()) == Nova::NOVA_TIMEOUT)
						_dispatcher.dispatch_device_wakeup();
				}
			}

			void update_deadline(Tsc const deadline)
			{
				Mutex::Guard guard(_mutex);

				bool const sooner_than_scheduled = (deadline.tsc < _deadline.tsc);

				_deadline = deadline;

				if (sooner_than_scheduled)
					if (_wakeup_sem.up() != Nova::NOVA_OK)
						error("unable to cancel already scheduled timeout");
			}
		} _waiter;

	public:

		Nova_device(Env &env, Tsc_rate tsc_rate, Wakeup_dispatcher &dispatcher)
		: _tsc_rate(tsc_rate), _waiter(env, dispatcher) { }

		Clock now() override
		{
			return _tsc_rate.clock_from_tsc( Tsc { Trace::timestamp() });
		}

		bool update_deadline(Deadline deadline) override
		{
			_waiter.update_deadline(_tsc_rate.tsc_from_clock(deadline));
			return true;
		}
};


struct Timer::Main : Nova_device::Wakeup_dispatcher
{
	Env &_env;

	Attached_rom_dataspace _platform_info { _env, "platform_info" };

	Tsc_rate const _tsc_rate = Tsc_rate::from_node(_platform_info.node());

	Nova_device _device { _env, _tsc_rate, *this };

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
		if (_tsc_rate.khz == 0)
			warning("could not obtain TSC calibration from platform_info ROM");

		_env.parent().announce(_env.ep().manage(_root));
	}
};


void Component::construct(Genode::Env &env) { static Timer::Main inst(env); }
