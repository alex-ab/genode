/*
 * \brief  Timer driver for the PIT
 * \author Norman Feske
 * \author Alexander Boettcher
 * \date   2024-05-13
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
#include <irq_session/connection.h>
#include <io_port_session/connection.h>
#include <timer/component.h>

namespace Timer {

	using namespace Genode;

	struct Pit_device;
	struct Main;
}


class Timer::Pit_device : Noncopyable, public Timer::Device
{
	private:

		enum {
			PIT_TICKS_PER_SECOND = 1193182,
			PIT_MAX_COUNT        =   65535,
			PIT_MAX_USEC         = (1000ull * 1000 * PIT_MAX_COUNT) /
			                       (PIT_TICKS_PER_SECOND),

			PIT_DATA_PORT_0      =    0x40,  /* data port for PIT channel 0,
			                                    connected to the PIC */
			PIT_CMD_PORT         =    0x43,  /* PIT command port */
			IRQ_PIT              =       0,  /* timer interrupt at the PIC */

			/*
			 * Bit definitions for accessing the PIT command port
			 */
			PIT_CMD_SELECT_CHANNEL_0 = 0 << 6,
			PIT_CMD_ACCESS_LO        = 1 << 4,
			PIT_CMD_ACCESS_LO_HI     = 3 << 4,
			PIT_CMD_MODE_IRQ         = 0 << 1,
			PIT_CMD_MODE_RATE        = 2 << 1,

			PIT_CMD_READ_BACK        = 3 << 6,
			PIT_CMD_RB_COUNT         = 0 << 5,
			PIT_CMD_RB_STATUS        = 0 << 4,
			PIT_CMD_RB_CHANNEL_0     = 1 << 1,

			/*
			 * Bit definitions of the PIT status byte
			 */
			PIT_STAT_INT_LINE = 1 << 7,
		};

		/* PIT counter */
		struct Counter { uint16_t value; };

	public:

		struct Wakeup_dispatcher : Interface
		{
			virtual void dispatch_device_wakeup() = 0;
		};

	private:

		Env &_env;

		Io_port_connection _io_port { _env, PIT_DATA_PORT_0,
		                              PIT_CMD_PORT - PIT_DATA_PORT_0 + 1 };

		Irq_connection _timer_irq { _env, unsigned(IRQ_PIT) };

		uint64_t _max_timeout_us { PIT_MAX_USEC };

		Wakeup_dispatcher &_dispatcher;

		Signal_handler<Pit_device> _handler { _env.ep(), *this, &Pit_device::_handle_timeout };

		uint64_t _curr_time_us      { };
		Counter  _last_read         { };
		bool     _wrap_handled      { };

		uint64_t _convert_counter_to_us(uint64_t counter)
		{
			/* round up to 1us in case of rest */
			auto const mod = (counter * 1000 * 1000) % PIT_TICKS_PER_SECOND;
			return (counter * 1000 * 1000 / PIT_TICKS_PER_SECOND)
			       + (mod ? 1 : 0);
		}

		Counter _convert_relative_us_to_counter(uint64_t rel_us)
		{
			return { .value = uint16_t(min(rel_us * PIT_TICKS_PER_SECOND / 1000 / 1000,
			                               uint64_t(PIT_MAX_COUNT))) };
		}

		void _handle_timeout()
		{
			_dispatcher.dispatch_device_wakeup();
			_timer_irq.ack_irq();
		}

		void _set_counter(Counter const &cnt)
		{
			/* wrap status gets reset by re-programming counter */
			_wrap_handled = false;

			_io_port.outb(PIT_DATA_PORT_0, uint8_t( cnt.value       & 0xff));
			_io_port.outb(PIT_DATA_PORT_0, uint8_t((cnt.value >> 8) & 0xff));
		}

		void _with_counter(auto const &fn)
		{
			/* read-back count of counter 0 */
			_io_port.outb(PIT_CMD_PORT, PIT_CMD_READ_BACK |
			                            PIT_CMD_RB_COUNT  |
			                            PIT_CMD_RB_STATUS |
			                            PIT_CMD_RB_CHANNEL_0);

			/* read status byte from latch register */
			uint8_t status = _io_port.inb(PIT_DATA_PORT_0);

			/* read low and high bytes from latch register */
			uint16_t lo = _io_port.inb(PIT_DATA_PORT_0);
			uint16_t hi = _io_port.inb(PIT_DATA_PORT_0);

			bool const wrapped = !!(status & PIT_STAT_INT_LINE);

			fn(Counter(uint16_t((hi << 8) | lo)), wrapped && !_wrap_handled);

			/* only handle wrap one time until next _set_counter */
			if (wrapped)
				_wrap_handled = true;
		}

		void _advance_current_time()
		{
			_with_counter([&](Counter const &pit, bool wrapped) {

				auto diff = (!wrapped && (_last_read.value >= pit.value))
				          ? _last_read.value - pit.value
				          : PIT_MAX_COUNT - pit.value + _last_read.value;

				_curr_time_us  += _convert_counter_to_us(diff);

				_last_read = pit;
			});
		}

	public:

		Pit_device(Env &env, Wakeup_dispatcher &dispatcher)
		: _env(env), _dispatcher(dispatcher)
		{
			/* operate PIT in one-shot mode */
			_io_port.outb(PIT_CMD_PORT, PIT_CMD_SELECT_CHANNEL_0 |
			              PIT_CMD_ACCESS_LO_HI | PIT_CMD_MODE_IRQ);

			_timer_irq.sigh(_handler);

			_handle_timeout();
		}

		Clock now() override
		{
			_advance_current_time();

			return Clock { .us = _curr_time_us };
		}

		bool update_deadline(Deadline const deadline) override
		{
			uint64_t const now_us = now().us;
			uint64_t const rel_us = (deadline.us > now_us)
			                      ? min(_max_timeout_us, deadline.us - now_us)
			                      : 1;

			auto const pit_cnt = _convert_relative_us_to_counter(rel_us);

			_last_read = pit_cnt;

			_set_counter(pit_cnt);

			return true;
		}
};


struct Timer::Main : Pit_device::Wakeup_dispatcher
{
	Env &_env;

	Pit_device _device { _env, *this };

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
