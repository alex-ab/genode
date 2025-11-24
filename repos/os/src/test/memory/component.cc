/*
 * \brief  Test memory
 * \author Alexander Boettcher
 * \date   2025-10-27
 *
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <timer_session/connection.h>

namespace Memory {

	using namespace Genode;

	class Main;
}


class Memory::Main
{
	private:

		typedef Constructible<Attached_dataspace> Attached_ds;

		static constexpr auto ds_count = 2;
		static constexpr auto ds_size  = 2 * 1024 * 1024;

		Env                      &env;
		Timer::Connection         timer     { env };
		Signal_handler<Main>      timeout   { env.ep(), *this, &Main::handle };

		Ram_dataspace_capability  mem_ds[ds_count] { };
		Attached_ds               memory[ds_count] { };

		bool                      stop_on_failure  { };

		void show_range(unsigned const mem_id,
		                auto const virt,
		                auto const range_start,
		                auto const range_count,
		                bool zero)
		{
			if (!range_count)
				return;

			log(mem_id, " virt", Hex_range(virt + range_start * sizeof(addr_t),
			    range_count * sizeof(addr_t)), " ",
			    zero ? "zero" : "not zero");
		}

		bool check_memory(unsigned const mem_id, Attached_dataspace &mem)
		{
			bool failure = false;

			addr_t range_start = 0;
			addr_t range_count = 0;
			bool   zero        = true;

			addr_t const virt = addr_t(mem.local_addr<addr_t>());

			for (unsigned i = 0; i < ds_size / sizeof(addr_t); i++, range_count++) {
				addr_t const value = mem.local_addr<addr_t>()[i];

				bool show = ( zero && value) || (!zero && !value);

				if (show) {
					failure = true;

					show_range(mem_id, virt, range_start, range_count, zero);

					range_count = 0;
					range_start = i;
					zero        = value == 0;
				}
			}

			show_range(mem_id, virt, range_start, range_count, zero);

			return !failure;
		}

	public:

		void handle()
		{
			bool fail = false;

			for (unsigned i = 0; i < ds_count; i++) {
				if (!check_memory(i, *memory[i])) fail = true;
			}

			log("memory check ", fail ? "failed" : "successful");

			if (fail && stop_on_failure)
				Genode::sleep_forever();
		}

		Main(Env &env) : env(env)
		{
			uint64_t period_us { 1'000'000 };

			Attached_rom_dataspace config { env, "config" };

			if (config.valid()) {
				auto const &node = config.node();

				stop_on_failure = node.attribute_value("stop_on_failure",
				                                       stop_on_failure);
				period_us = node.attribute_value("period_us", period_us);
			}

			log("starting periodic memory test");
			log(" - period ", period_us, "us");
			log(" - stop_on_failure=", stop_on_failure);

			for (auto &ds : mem_ds) ds = env.ram().alloc(ds_size);

			for (unsigned i = 0; i < ds_count; i++) {
				auto &mem = memory[i];

				mem.construct(env.rm(), mem_ds[i]);

				auto const p_addr = env.pd().dma_addr(mem_ds[i]);

				log("physical address of memory region ", i, " - ",
				    Hex(p_addr), !p_addr ? " - probably wrong!" : "");
				log("virtual  address of memory region ", i, " - ",
				    Hex_range(addr_t(mem->local_addr<addr_t>()), ds_size));
			}

			timer.sigh(timeout);

			timer.trigger_periodic(period_us);
		}
};

void Component::construct(Genode::Env &env) { static Memory::Main main(env); }
