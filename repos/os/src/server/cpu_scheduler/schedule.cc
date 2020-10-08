/*
 * \brief  Taking schedule decision depending on policy/scheduler algorithm
 * \author Alexander Boettcher
 * \date   2020-07-16
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

void Cpu::Session::iterate_threads(Trace &trace, Session_label const &cpu_scheduler)
{
	apply([&](Thread_capability const &cap,
	          Affinity::Location &stored_loc,
	          Name const &name,
	          enum POLICY const &policy,
	          Subject_id &subject_id)
	{
		if (!subject_id.id) {
			Session_label const label(cpu_scheduler, " -> ", _label);
			subject_id = trace.lookup_missing_id(label, name);
		}

		if (!subject_id.id) {
			Genode::error("subject id ", name, " missing - still !!!!");
			return false;
		}

		Affinity::Location const &base = _affinity.location();
		Affinity::Location current { base.xpos() + stored_loc.xpos(),
		                             base.ypos() + stored_loc.ypos(), 1, 1 };

		/* request execution time and current location */
		trace.retrieve(subject_id.id, [&] (Execution_time const time,
		                                   Affinity::Location const current_loc)
		{
#if 0
			if (_verbose)
				log("[", _label, "] currently at ",
				    current_loc.xpos(), "x", current_loc.ypos(),
				    ", name='", name, "' -",
				    " ec/sc time=", time.thread_context, "/",
				                    time.scheduling_context);
#else
			(void)time;
#endif
			current = current_loc;
		});

		/* update current location of thread if changed */
		if (policy != POLICY_PIN) {
			if (current.xpos() != stored_loc.xpos() + base.xpos() ||
			    current.ypos() != stored_loc.ypos() + base.ypos())
			{
				if (current.xpos() >= base.xpos() && current.ypos() >= base.ypos()) {

					unsigned const xpos = current.xpos() - base.xpos();
					unsigned const ypos = current.ypos() - base.ypos();

					if (xpos < base.width() && ypos < base.height()) {
						stored_loc = Affinity::Location(xpos, ypos);
						_report = true;
					} else
						Genode::error("affinity dimension raised");
				} else
					Genode::error("affinity location strange, current below base");
			}
		}

		if (policy == POLICY_NONE)
			return false;

		if (policy != POLICY_MAX_UTILIZE) {
			_schedule(cap, stored_loc, current, name, policy);
			return false;
		}

		Execution_time     most_idle { 0UL, 0UL };
		Affinity::Location loc_idle = current; /* in case of no idle info */

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

		if ((loc_idle.xpos() != current.xpos()) ||
		    (loc_idle.ypos() != current.ypos()))
		{
			if (_verbose)
				log("[", _label, "] name='", name, "' request to",
				    " migrate from ", current.xpos(), "x", current.ypos(),
				    " to most idle CPU at ",
				    loc_idle.xpos(), "x", loc_idle.ypos());

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
	          enum POLICY const &policy,
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
                             enum POLICY const &policy)
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
			Location rel { int((relativ.xpos() + 1) % base.width()),
			               int(relativ.ypos() % base.height()), 1, 1 };

			migrate_to = Location { int(base.xpos() + rel.xpos()),
			                        base.ypos() + rel.ypos(), 1, 1 };
			migrate = true;

			if (_verbose)
				log("[", _label, "] name='", name, "' request to",
				    " migrate from ", relativ.xpos(), "x", relativ.ypos(),
				    " to ", rel.xpos(), "x", rel.ypos(),
				    " (", migrate_to.xpos(), "x", migrate_to.ypos(), ")");
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
	                   enum POLICY &policy)
	{
		policy = string_to_policy(scheduler);

		if (policy == POLICY_PIN) {
			if (location.xpos() != relativ.xpos() ||
			    location.ypos() != relativ.ypos())
				_report = true;

			location = relativ;
		}

		if (_verbose) {
			String<12> const loc { location.xpos(), "x", location.ypos() };
			log("[", _label, "] name='", thread, "' "
			    "update policy to '", policy_to_string(policy), "' ",
			    (policy == POLICY_PIN) ? loc : String<12>());
		}

		found = true;
		return true;
	});

	if (found)
		return;

	construct([&](Thread_capability const &,
	              Affinity::Location &location,
	              Name &name,
	              enum POLICY &policy)
	{
		policy = string_to_policy(scheduler);
		name = thread;

		if (policy == POLICY_PIN)
			location = relativ;

		if (_verbose) {
			String<12> const loc { location.xpos(), "x", location.ypos() };
			log("[", _label, "] name='", thread, "' "
			    "new policy '", policy_to_string(policy), "' ",
			    (policy == POLICY_PIN) ? loc : String<12>());
		}

		return true;
	});
}
