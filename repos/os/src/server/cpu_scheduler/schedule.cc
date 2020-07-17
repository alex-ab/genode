/*
 * \brief  Taking schedule decision depending on policy/scheduler algorithm
 * \author Alexander Boettcher
 * \date   2020-24-16
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <cpu_thread/client.h>

#include "session.h"
#include "trace.h"

void Cpu::Session::iterate_threads(Trace &trace)
{
	apply([&](Thread_capability const &cap,
	          Affinity::Location &loc,
	          Name const &name,
	          uint8_t const &policy,
	          Subject_id &subject_id)
	{
		if (policy == POLICY_NONE)
			return false;

		if (!subject_id.id) {
			/* XXX - configure option req, how to find out or better way ? */
			Session_label const label("init -> cpu_scheduler -> ", _label);
			subject_id = trace.lookup_missing_id(label, name);
		}

		if (!subject_id.id) {
			Genode::error("subject id ", name, " missing - still !!!!");
			return false;
		}

		/* request execution time and current location */
		Affinity::Location current = loc;
		trace.retrieve(subject_id.id, [&] (Execution_time const time,
		                                   Affinity::Location const current_loc)
		{
			Genode::log(name, " - ", time.thread_context, "/",
			            time.scheduling_context, " ",
			            current_loc.xpos(), "x", current_loc.ypos());
			current = current_loc;
		});

		if (policy != POLICY_MAX_UTILIZE) {
			_schedule(cap, loc, current, name, policy);
			return false;
		}

/*
		Genode::error("---- ", _affinity.location().xpos(), "x",
		                       _affinity.location().ypos(), " ",
		                       _affinity.space().width(), "x",
		                       _affinity.space().height());
*/
/*
		Affinity::Space    const &space = _affinity.space();
		for (unsigned x = 0; x < space.width(); x++) {
			for (unsigned y = 0; y < space.height(); y++) {
*/
		Execution_time     most_idle { 0UL, 0UL };
		Affinity::Location loc_idle = current; /* in case of no idle info */

		Affinity::Location const &base = _affinity.location();
		for (unsigned x = base.xpos(); x < base.xpos() + base.width(); x++) {
			for (unsigned y = base.ypos(); y < base.ypos() + base.height(); y++) {
				Affinity::Location const loc(x, y);
				Execution_time const idle = trace.diff_idle_times(loc);

				if (idle.scheduling_context) {
					if (idle.scheduling_context > most_idle.scheduling_context) {
						most_idle = idle;
						loc_idle  = loc;
					}
				} else {
					if (idle.thread_context > most_idle.thread_context) {
						most_idle = idle;
						loc_idle  = loc;
					}
				}
			}
		}

		/* update current location of thread */
		if (current.xpos() != loc.xpos() + base.xpos() ||
		    current.ypos() != loc.ypos() + base.ypos())
		{
			if (current.xpos() >= base.xpos() && current.ypos() >= base.ypos()) {

				unsigned const xpos = current.xpos() - base.xpos();
				unsigned const ypos = current.ypos() - base.ypos();

				if (xpos < base.width() && ypos < base.height()) {
					loc = Affinity::Location(xpos, ypos);
					_report = true;
				} else
					Genode::error("affinity dimension raised");
			} else
				Genode::error("affinity location strange, current below base");
		}

		Genode::warning("loc most idle ", loc_idle.xpos(), "x", loc_idle.ypos(),
		                " ", current.xpos(), "x", current.ypos());

		if ((loc_idle.xpos() != current.xpos()) ||
		    (loc_idle.ypos() != current.ypos()))
		{
			Cpu_thread_client thread(cap);
			thread.affinity(loc_idle);
		}
		return false;
	});
}

void Cpu::Session::iterate_threads()
{
	apply([&](Thread_capability const &cap,
	          Affinity::Location &loc,
	          Name const &name,
	          uint8_t const &policy,
	          Subject_id &)
	{
		_schedule(cap, loc, loc, name, policy);

		return false;
	});
}

void Cpu::Session::_schedule(Thread_capability const &cap,
                             Affinity::Location &relativ,
                             Affinity::Location &current,
                             Name const &name,
                             uint8_t const &policy)
{
	typedef Affinity::Location Location;

	if (!cap.valid())
		return;

	if (policy == POLICY_NONE)
		return;

	Location const &base = _affinity.location();

	bool migrate = false;
	Location migrate_to = Location(base.xpos() + relativ.xpos(),
	                               base.ypos() + relativ.ypos());

	switch (policy) {
	case POLICY_PIN:
		{
			/* check that we are on right CPU and if not try to migrate */
			migrate = (migrate_to.xpos() != current.xpos()) ||
			          (migrate_to.ypos() != current.ypos());
			break;
		}
	case POLICY_ROUND_ROBIN:
		{
			relativ = Location { int((relativ.xpos() + 1) % base.width()),
			                     int(relativ.ypos() % base.height()), 1, 1 };

			migrate_to = Location { int(base.xpos() + relativ.xpos()),
			                        base.ypos() + relativ.ypos(), 1, 1 };
			migrate = true;

			Genode::error(this, " - migrate ", name, " ",
			              migrate_to.xpos(), "x", migrate_to.ypos(), " ",
			              relativ.xpos(), "x", relativ.ypos());
			break;
		}
	default:
		break;
	}

	if (migrate) {
		Cpu_thread_client thread(cap);
		thread.affinity(migrate_to);
	}
}

void Cpu::Session::config(Name const &thread, Name const &scheduler,
                          Affinity::Location const &relativ)
{
	bool found = false;

	lookup(thread, [&](Thread_capability const &,
	                   Affinity::Location &location,
	                   uint8_t &policy)
	{
		if (scheduler == "pin") {
			policy = POLICY_PIN;
			location = relativ;
		} else
		if (scheduler == "round-robin")
			policy = POLICY_ROUND_ROBIN;
		else
		if (scheduler == "max-utilize")
			policy = POLICY_MAX_UTILIZE;
		else
			policy = POLICY_NONE;

		Genode::error("found check ", policy, " '", scheduler, "'", " ", thread);

		found = true;
		return true;
	});

	if (found)
		return;

	construct([&](Thread_capability const &,
	              Affinity::Location &location,
	              Name &name,
	              uint8_t &policy)
	{
		name = thread;

		if (scheduler == "pin") {
			policy = POLICY_PIN;
			location = relativ;
		} else
		if (scheduler == "round-robin")
			policy = POLICY_ROUND_ROBIN;
		else
		if (scheduler == "max-utilize")
			policy = POLICY_MAX_UTILIZE;
		else
			policy = POLICY_NONE;

		Genode::error("wrote invalid ", policy, " '", scheduler, "'", " ", name, " ", location.xpos(), "x", location.ypos());
		return true;
	});
}
