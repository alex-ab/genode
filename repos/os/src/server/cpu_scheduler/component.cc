/*
 * \author Alexander Boettcher
 * \date   2020-07-16
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/signal.h>

#include <cpu_session/cpu_session.h>
#include <timer_session/connection.h>

#include "session.h"
#include "config.h"
#include "trace.h"

namespace Cpu {
	struct Scheduler;

	using Genode::Affinity;
	using Genode::Attached_rom_dataspace;
	using Genode::Constructible;
	using Genode::Cpu_session;
	using Genode::Insufficient_ram_quota;
	using Genode::Ram_quota;
	using Genode::Rpc_object;
	using Genode::Session_capability;
	using Genode::Signal_handler;
	using Genode::Sliced_heap;
	using Genode::Typed_root;
}

struct Cpu::Scheduler : Rpc_object<Typed_root<Cpu_session>>
{
	Genode::Env            &env;
	Attached_rom_dataspace  config   { env, "config" };
	Timer::Connection       timer    { env };
	Sliced_heap             heap     { env.ram(), env.rm() };
	Child_list              list     { };
	Constructible<Trace>    trace    { };
	Constructible<Reporter> reporter { };
	uint64_t                timer_us { 1000 * 1000UL };
	Session_label           label    { };
	bool                    verbose  { false };

	Signal_handler<Scheduler> signal_config {
		env.ep(), *this, &Scheduler::handle_config };

	void handle_config();
	void handle_timeout();

	/*
	 * Need extra EP to avoid dead-lock/live-lock (depending on kernel)
	 * due to down-calls by this component, e.g. parent.upgrade(...), and
	 * up-calls by parent using this CPU service, e.g. to create initial thread
	 *
	 * Additionally, a list_mutex is required due to having 2 EPs now.
	 */
	Entrypoint ep { env, 4 * 4096, "live/dead-lock", Affinity::Location() };

	Signal_handler<Scheduler> signal_timeout {
		ep, *this, &Scheduler::handle_timeout };

	Genode::Mutex list_mutex { };

	/***********************
	 ** Session interface **
	 ***********************/

	Session_capability session(Root::Session_args const &args,
	                           Affinity const &affinity) override
	{
		/* cap quota removal XXX ? */
		Ram_quota const ram_quota = Genode::ram_quota_from_args(args.string());

		if (verbose)
			Genode::log("new session ", args.string(), " ",
			            affinity.space().width(), "x", affinity.space().height(), " ",
			            affinity.location().xpos(), "x", affinity.location().ypos(),
			            " ", affinity.location().width(), "x", affinity.location().height());

		Genode::size_t const session_size = Genode::max(4096UL, sizeof(Session));
		/* when this trigger we have to think about dynamic memory allocation */
//		static_assert(session_size <= 4096);

		if (ram_quota.value < session_size)
			throw Insufficient_ram_quota();

		/* XXX remove session_size from args */
		Mutex::Guard guard(list_mutex);

		Session * session = new (heap) Session(env, affinity, args, list, verbose);

		/* check for config of new session */
		Cpu::Config import;
		import.apply(config.xml(), list);

		return session->cap();
	}

	void upgrade(Session_capability const cap, Root::Upgrade_args const &args) override
	{ 
		if (!args.valid_string()) return;

		auto lambda = [&] (Rpc_object_base *rpc_obj) {
			if (!rpc_obj)
				return;

			Session *session = dynamic_cast<Session *>(rpc_obj);
			if (!session)
				return;

			session->upgrade([&](auto id) {
				env.upgrade(id, args);
			});
		};
		env.ep().rpc_ep().apply(cap, lambda);
	}

	void close(Session_capability const cap) override
	{
		if (!cap.valid()) return;

		Session *object = nullptr;

		env.ep().rpc_ep().apply(cap,
			[&] (Session *source) {
				object = source;
		});

		if (object) {
			Mutex::Guard guard(list_mutex);
			destroy(heap, object);
		}
	}

	/*****************
	 ** Constructor **
	 *****************/

	Scheduler(Genode::Env &env) : env(env)
	{
		config.sigh(signal_config);
		timer.sigh(signal_timeout);

		Affinity::Space const space = env.cpu().affinity_space();
		Genode::log("scheduler affinity space=",
		            space.width(), "x", space.height());

		handle_config();

		/* first time start ever timeout */
		timer.trigger_periodic(timer_us);

		env.parent().announce(env.ep().manage(*this));
	}
};

void Cpu::Scheduler::handle_config()
{
	config.update();

	bool use_trace   = true;
	bool use_report  = true;
	uint64_t time_us = timer_us;

	if (config.valid()) {
		use_trace   = config.xml().attribute_value("trace", use_trace);
		use_report  = config.xml().attribute_value("report", use_report);
		time_us     = config.xml().attribute_value("interval_us", timer_us);
		verbose     = config.xml().attribute_value("verbose", verbose);

		/* read in components configuration */
		Cpu::Config import;
		import.apply(config.xml(), list);
	}

	if (verbose)
		log("config update - verbose=", verbose, ", trace=", use_trace,
		    ", report=", use_report, ", interval=", timer_us,"us");

	/* also start all subsystem if no valid config is available */
	if (use_trace) {
		if (!trace.constructed())
			trace.construct(env);
		if (!label.valid())
			label = trace->lookup_my_label();
	} else
		trace.destruct();

	if (use_report) {
		if (!reporter.constructed()) {
			reporter.construct(env, "components", "components", 4096 * 1);
			reporter->enabled(true);
		}
	} else
		reporter.destruct();

	if (timer_us != time_us)
		timer.trigger_periodic(time_us);
}

void Cpu::Scheduler::handle_timeout()
{
	if (trace.constructed())
		trace->read_idle_times();

	Mutex::Guard guard(list_mutex);

	bool report_update = false;

	for (auto x = list.first(); x; x = x ->next()) {
		auto session = x->object();
		if (!session)
			continue;

		if (trace.constructed()) {
			session->iterate_threads(*trace, label);
		}
		else
			session->iterate_threads();

		if (session->report_update())
			report_update = true;
	}

	if (reporter.constructed() && report_update) {
		Reporter::Xml_generator xml(*reporter, [&] () {
			for (auto x = list.first(); x; x = x ->next()) {
				auto session = x->object();
				if (!session)
					continue;

				session->report_state(xml);
			}
		});
	}

}

void Component::construct(Genode::Env &env)
{
	static Cpu::Scheduler server(env);
}
