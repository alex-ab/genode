/*
 * \brief  Timer driver for Fiasco.OC
 * \author Norman Feske
 * \author Alexander Boettcher
 * \date   2024-06-16
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

/* Fiasco.OC includes */
#include <foc/native_thread.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"

namespace Foc {
	#include <l4/sys/ipc.h>
	#include <l4/sys/kip.h>
}

#pragma GCC diagnostic pop


using namespace Foc;


namespace Timer {

	using namespace Genode;

	class  Foc_device;
	struct Main;
}


class Timer::Foc_device : public Timer::Device
{
	private:

		Attached_rom_dataspace _kip_ds;

	public:

		struct Wakeup_dispatcher : Interface
		{
			virtual void dispatch_device_wakeup() = 0;
		};

	private:

		struct Waiter : Thread
		{
			l4_timeout_s mus_to_timeout(uint64_t const mus) const
			{
				if (mus == 0)
					return L4_IPC_TIMEOUT_0;
				else if (mus == ~0ULL)
					return L4_IPC_TIMEOUT_NEVER;

				long e = Genode::log2(mus, 0u) - 7;

				if (e < 0) e = 0;

				uint64_t m = mus / (1UL << e);

				enum { M_MASK = 0x3ff };

				/* check corner case */
				if ((e > 31 ) || (m > M_MASK)) {
					Genode::warning("invalid timeout ", mus, ", using max. values");
					e = 0;
					m = M_MASK;
				}

				return l4_timeout_rel(m & M_MASK, (unsigned)e);
			}

			Wakeup_dispatcher &_dispatcher;

			Mutex    _mutex    { }; /* protect '_deadline' */
			Deadline _deadline { ~0ULL };

			l4_cap_idx_t _myself { };

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
				with_native_thread([&] (Native_thread &nt) { _myself = nt.kcap; });

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
							/* due to ex_regs not working, use 1ms granularity */
							auto usecs = min(deadline.us - now.us, 1000ull);

							auto const timeout = l4_timeout(L4_IPC_TIMEOUT_NEVER,
							                                mus_to_timeout(usecs));
							l4_ipc_sleep(timeout);
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

					/* XXX not working, syscall does not return
					Foc::l4_thread_ex_regs(_myself, ~0UL, ~0UL,
					                       L4_THREAD_EX_REGS_CANCEL);
					 */
				}
			}
		} _waiter;

	public:

		Foc_device(Env &env, Wakeup_dispatcher &dispatcher)
		: _kip_ds(env, "l4v2_kip"), _waiter(env, dispatcher, *this) { }

		Clock now() override
		{
			auto kip = _kip_ds.local_addr<Foc::l4_kernel_info_t>();
			return { .us = Foc::l4_kip_clock(kip) };
		}

		bool update_deadline(Deadline deadline) override
		{
			_waiter.update_deadline(deadline);
			return true;
		}
};


struct Timer::Main : Foc_device::Wakeup_dispatcher
{
	Env &_env;

	Foc_device _device { _env, *this };

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
