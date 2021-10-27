/*
 * \author Alexander Boettcher
 * \date   2021-10-23
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/register.h>

#include "temp_freq.h"
#include "power.h"

namespace Genode {

	static inline void print(Output &out, Affinity::Location location)
	{
		print(out, location.xpos(), ",", location.ypos());
	}
}


using namespace Genode;

struct Core_thread : Thread, Msr::Monitoring
{
	/*
	 * Noncopyable
	 */
	Core_thread(Core_thread const &);
	Core_thread &operator = (Core_thread const &);

	Location const location;
	Blockade       barrier { };
	Blockade       done { };

	uint64_t const tsc_freq_khz;

	Constructible<Msr::Power> power { };

	bool master { };

	Xml_node const *config_node { };

	Core_thread(Env &env, Location const &location, uint64_t tsc_freq_khz)
	:
		Thread(env, Name("msr", location), 4 * 4096 /* STACK_SIZE */,
	           location, Weight(), env.cpu()),
		location(location), tsc_freq_khz(tsc_freq_khz)
	{ }

	void entry() override
	{
		/* create object by thread per CPU */
		power.construct();

		Nova::Utcb &utcb = *reinterpret_cast<Nova::Utcb *>(Thread::utcb());

		if (master)
			Monitoring::target_temperature(utcb); 

		while (true) {
			barrier.block();

			Monitoring::update_cpu_temperature(utcb);
			Monitoring::cpu_frequency(utcb, tsc_freq_khz);
			if (master)
				Monitoring::update_package_temperature(utcb);

			power->update(utcb);

			if (config_node) {
				power->update(utcb, *config_node);
				config_node = nullptr;
			}

			done.wakeup();
		}
	}

};

struct Msr::Msr {

	/*
	 * Noncopyable
	 */
	Msr(Msr const &);
	Msr &operator = (Msr const &);

	Env                &env;
	Heap                heap     { env.ram(), env.rm() };
	Timer::Connection   timer    { env };
	Signal_handler<Msr> handler  { env.ep(), *this, &Msr::handle_timeout };
	Reporter            reporter { env, "info", "info", 2 * 4096 };

	Affinity::Space     cpus     { env.cpu().affinity_space() };
	Core_thread **      threads  { new (heap) Core_thread*[cpus.total()] };

	Attached_rom_dataspace config { env, "config" };

	Signal_handler<Msr> signal_config {
		env.ep(), *this, &Msr::handle_config };

	Msr(Genode::Env &env) : env(env)
	{
		Attached_rom_dataspace info { env, "platform_info"};

		uint64_t freq_khz = info.xml().sub_node("hardware")
		                              .sub_node("tsc")
		                              .attribute_value("freq_khz", 0UL);

		String<16> kernel = info.xml().sub_node("kernel")
		                              .attribute_value("name", String<16>());

		log("Detected: ", kernel, " kernel, ", cpus.width(), "x",
		    cpus.height(), " CPU", cpus.total() > 1 ? "s" : "",
		    ", TSC ", freq_khz, " kHz");

		if (kernel != "nova") {
			error("kernel not supported");
			return;
		}

		/* XXX get msr cap applied to Nova::SM_MSR */
		Genode::Pd_session::Managing_system_state state {};
		env.pd().managing_system(state);

		/* construct the thread objects */
		for (unsigned y = 0; y < cpus.height(); y++) {
			for (unsigned x = 0; x < cpus.width(); x++) {
				unsigned const i = x + y * cpus.width();
				threads[i] = new (heap) Core_thread(env, Affinity::Location(x, y),
				                                    freq_khz);

				/* the first thread will read out TCC && package temperature */
				if (x == 0 && y == 0)
					threads[i]->master = true;

				threads[i]->start();
			}
		}

		timer.sigh(handler);
		timer.trigger_periodic(5000 * 1000 /* us */);

		handle_config();

		reporter.enabled(true);
	}

	void handle_timeout()
	{
		for (unsigned i = 0; i < cpus.total(); i++) {
			threads[i]->barrier.wakeup();
		}

		for (unsigned i = 0; i < cpus.total(); i++) {
			threads[i]->done.block();
		}

		Reporter::Xml_generator xml(reporter, [&] () {
			/* XXX with this curr_time() we are not woken up periodically anymore ?! */
//			xml.attribute("time", timer.curr_time().trunc_to_plain_ms().value);

			/* XXX per package value handling
			 * target temperature is identical over a package
			 */
			unsigned tcc = 0;

			Core_thread const &package = *threads[0];
			if (package.temp_tcc_valid) {
				tcc = package.temp_tcc;
				xml.attribute("tcc_temp_c", tcc);
			}

			if (tcc && package.temp_package_valid)
				xml.attribute("pkg_temp_c", tcc - package.temp_package);

			for (unsigned i = 0; i < cpus.total(); i++) {
				Core_thread const &cpu = *threads[i];

				xml.node("cpu", [&] () {
					xml.attribute("x", cpu.location.xpos());
					xml.attribute("y", cpu.location.ypos());
					cpu.report(xml, tcc);
					cpu.power->report(xml);
				});
			}
		});
	}

	void handle_config()
	{
		config.update();

		if (!config.valid())
			return;

		config.xml().for_each_sub_node("cpu", [&](Xml_node const &node) {
			if (!node.has_attribute("x") || !node.has_attribute("y"))
				return;
			unsigned const xpos  = node.attribute_value("x", 0u);
			unsigned const ypos  = node.attribute_value("y", 0u);
			unsigned const index = xpos + ypos * cpus.width();
			if (index >= cpus.total())
				return;

			Core_thread &cpu = *threads[index];

			if (!cpu.power.constructed())
				return;

			cpu.config_node = &node;

			cpu.barrier.wakeup();
			cpu.done.block();
		});
	}
};

void Component::construct(Env &env) { static Msr::Msr component(env); }
