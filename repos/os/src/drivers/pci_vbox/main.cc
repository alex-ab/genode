/*
 * \brief  VBox PCI driver
 * \author Josef Soentgen
 * \date   2021-01-07
 *
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/allocator_avl.h>
#include <base/attached_ram_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <block/request_stream.h>
#include <dataspace/client.h>
#include <os/attached_mmio.h>
#include <os/reporter.h>
#include <os/session_policy.h>
#include <root/root.h>
#include <timer_session/connection.h>
#include <util/bit_array.h>
#include <util/interface.h>
#include <util/misc_math.h>

#include <trace/timestamp.h>

/* local includes */
#include <util.h>
#include <pci.h>


namespace {

using uint16_t = Genode::uint16_t;
using uint32_t = Genode::uint32_t;
using uint64_t = Genode::uint64_t;
using size_t   = Genode::size_t;
using addr_t   = Genode::addr_t;

} /* anonymous namespace */


namespace Vbox {
	using namespace Genode;

	struct Controller;
	struct Driver;
	struct Main;
};


/*
 * Controller
 */
struct Vbox::Controller : public Genode::Attached_mmio
{
	/**********
	 ** MMIO **
	 **********/

	struct Config : Register<0x04, 32>
	{
		struct Enable   : Bitfield< 0,  1> { };
		struct Interval : Bitfield< 1, 31> { };
	};

	struct Status : Register<0x08, 64>
	{
		struct Ready               : Bitfield< 0,  1> { };
		struct Error               : Bitfield< 1,  1> { };
		struct Count               : Bitfield< 2, 16> { };
		struct Interrupt_pending   : Bitfield<18,  1> { };
		struct Interrupt_timestamp : Bitfield<31, 32> { };
	};

	struct Initialization_failed : Genode::Exception { };

	Genode::Env &_env;

	Mmio::Delayer       &_delayer;

	void _wait_for_rdy(unsigned val)
	{
		enum { MAX = 50u, TO_UNIT = 500u, };
		Attempts     const a(MAX);
		Microseconds const t(TO_UNIT);
		try {
			wait_for(a, t, _delayer, Status::Ready::Equal(val));
		} catch (Mmio::Polling_timeout) {
			error("Status::Ready(", val, ") failed");
			throw;
		}
	}

	void _reset()
	{
		write<Config>(0);

		try { _wait_for_rdy(0); }
		catch (...) { throw Initialization_failed(); }
	}

	Controller(Genode::Env &env,
	           addr_t const base, size_t const size,
	           Mmio::Delayer &delayer)
	:
		Genode::Attached_mmio(env, base, size),
		_env(env), _delayer(delayer)
	{ }

	void init()
	{
		_reset();

		write<Config::Enable>(1);

		try { _wait_for_rdy(1); }
		catch (...) {
			if (read<Status::Error>()) {
				error("fatal controller status");
			}
			throw Initialization_failed();
		}
	}

	void clear_intr()
	{
		write<Status::Interrupt_pending>(1);
	}

	void set_interval(uint64_t us)
	{
		write<Config::Interval>(us);
	}

	uint64_t status() const
	{
		return read<Status>();
	}
};


class Vbox::Driver : Genode::Noncopyable
{
	public:

		struct Io_error           : Genode::Exception { };
		struct Request_congestion : Genode::Exception { };

	private:

		Driver(const Driver&) = delete;
		Driver& operator=(const Driver&) = delete;

		Genode::Env       &_env;
		Genode::Allocator &_alloc;

		Genode::Attached_rom_dataspace &_config_rom;

		Genode::uint64_t _freq_mhz { 2100 };

		void _handle_config_update()
		{
			_config_rom.update();

			if (!_config_rom.valid()) { return; }
		}

		Genode::Signal_handler<Driver> _config_sigh {
			_env.ep(), *this, &Driver::_handle_config_update };

		addr_t _dma_base { 0 };

		Genode::Constructible<Vbox::Pci> _vbox_pci { };

		/*********************
		 ** MMIO Controller **
		 *********************/

		struct Timer_delayer : Genode::Mmio::Delayer,
		                       Timer::Connection
		{
			Timer_delayer(Genode::Env &env)
			: Timer::Connection(env) { }

			void usleep(uint64_t us) override { Timer::Connection::usleep(us); }
		} _delayer { _env };

		Genode::Constructible<Vbox::Controller> _vbox_controller { };

		template<typename FN>
		void measure_time(char const *label, FN const &fn)
		{
			uint64_t const t1 = Genode::Trace::timestamp();
			fn();
			uint64_t const t2 = Genode::Trace::timestamp();
			Genode::log(label, ": ", (t2 - t1) / _freq_mhz, " us");
		}

	public:

		/**
		 * Constructor
		 */
		Driver(Genode::Env                       &env,
		       Genode::Allocator                 &alloc,
		       Genode::Attached_rom_dataspace    &config_rom,
		       Genode::Signal_context_capability  request_sigh)
		: _env(env), _alloc(alloc), _config_rom(config_rom)
		{
			_config_rom.sigh(_config_sigh);
			_handle_config_update();

			try {
				Genode::Attached_rom_dataspace info { env, "platform_info"};

				Genode::uint64_t result = info.xml().sub_node("hardware")
				                          .sub_node("tsc")
				                          .attribute_value("freq_khz", 0ULL);
				_freq_mhz = result / 1000;
			} catch (...) { }

			log("tsc frequency: ", _freq_mhz, "MHz");

			try {
				_vbox_pci.construct(_env);
			} catch (Vbox::Pci::Missing_controller) {
				error("no Vbox PCIe controller found");
				throw;
			}

			_vbox_controller.construct(_env, _vbox_pci->base(),
			                           _vbox_pci->size(), _delayer);

			_vbox_controller->init();

			uint64_t const interval_us = 2000;

			Genode::log("Interval: ", interval_us, " us");
			measure_time("set_interval: ", [&] () {
				_vbox_controller->set_interval(interval_us);
			});

			_vbox_pci->sigh_irq(request_sigh);

			_vbox_controller->clear_intr();
		}

		~Driver() { /* free resources */ }

		Genode::Ram_dataspace_capability dma_alloc(size_t size)
		{
			Genode::Ram_dataspace_capability cap = _vbox_pci->alloc(size);
			_dma_base = Dataspace_client(cap).phys_addr();
			return cap;
		}

		void dma_free(Genode::Ram_dataspace_capability cap)
		{
			_dma_base = 0;
			_vbox_pci->free(cap);
		}


		/**********************
		 ** driver interface **
		 **********************/

		enum { MAX_EXECUTES = 1000, };
		struct Exec_status {
			uint64_t diff_ts;
			uint64_t local_ts;
			uint64_t local_counter;

			uint64_t status;
			uint64_t status_ts;
			uint64_t status_counter;

			void print(Genode::Output &out) const
			{
				Genode::print(out, diff_ts, "(", local_ts, ",", local_counter, ") (",
				              status_ts, ",", status_counter, ") ", Genode::Hex(status));
			}
		};
		Exec_status _execute_ts[MAX_EXECUTES] { };

		uint64_t _execute_counter { 0 };
		uint64_t _last_ts         { 0 };
		uint64_t _last_global_ts  { 0 };

		void execute()
		{
			uint64_t const ts = Genode::Trace::timestamp();

			uint64_t const status = _vbox_controller->status();
			_vbox_controller->clear_intr();

			Exec_status es {
				.diff_ts        = (ts - _last_ts) / _freq_mhz,
				.local_ts       = ts,
				.local_counter  = _execute_counter + 1,
				.status         = status,
				.status_ts      = Controller::Status::Interrupt_timestamp::get(status),
				.status_counter = Controller::Status::Count::get(status)
			};
			_execute_ts[_execute_counter % MAX_EXECUTES] = es;

			_last_ts = ts;

			if (_execute_counter % MAX_EXECUTES == 0) {
				Genode::log(__func__, ": ", (unsigned)MAX_EXECUTES, " execs in: ",
				            (ts - _last_global_ts)/_freq_mhz, " us");
				_last_global_ts = ts;

				for (size_t i = 0; i < 4; i++) {
					Genode::log(__func__, ": ts[",i,"]: ", _execute_ts[i]);
				}
			}

			++_execute_counter;

			_vbox_pci->ack_irq();
		}
};


/**********
 ** Main **
 **********/

struct Vbox::Main
{
	Genode::Env  &_env;
	Genode::Heap  _heap { _env.ram(), _env.rm() };

	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	Constructible<Vbox::Driver> _driver { };

	Signal_handler<Main> _irq_handler { _env.ep(), *this, &Main::_handle_irq };

	void _handle_irq()
	{
		_driver->execute();
	}

	Main(Genode::Env &env) : _env(env)
	{
		_driver.construct(_env, _heap, _config_rom, _irq_handler);
	}
};


void Component::construct(Genode::Env &env) { static Vbox::Main main(env); }
