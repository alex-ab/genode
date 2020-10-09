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
	          Name const &name,
	          Subject_id &subject_id,
	          Cpu::Policy &policy)
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
		Affinity::Location current { base.xpos() + policy.location.xpos(),
		                             base.ypos() + policy.location.ypos(), 1, 1 };

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
		if (policy.update(base, current))
			_report = true;

		Affinity::Location migrate_to = current;
		if (policy.migrate(_affinity.location(), migrate_to, &trace)) {
//			if (_verbose)
				log("[", _label, "] name='", name, "' request to",
				    " migrate from ", current.xpos(), "x", current.ypos(),
				    " to most idle CPU at ",
				    migrate_to.xpos(), "x", migrate_to.ypos());

			Cpu_thread_client thread(cap);
			thread.affinity(migrate_to);
		}

		return false;
	});
}

void Cpu::Session::iterate_threads()
{
	apply([&](Thread_capability const &cap,
	          Name const &name,
	          Subject_id &,
	          Cpu::Policy const &policy)
	{
		Affinity::Location const &base = _affinity.location();
		Location current = Location(base.xpos() + policy.location.xpos(),
		                            base.ypos() + policy.location.xpos(),
		                            1, 1);
		_schedule(cap, current, name, policy);

		return false;
	});
}

void Cpu::Session::_schedule(Thread_capability const &cap,
                             Affinity::Location const &current,
                             Name const &,
                             Cpu::Policy const &policy)
{
	if (!cap.valid())
		return;

	Affinity::Location migrate_to = current;
	if (!policy.migrate(_affinity.location(), migrate_to, nullptr))
		return;

	Cpu_thread_client thread(cap);
	thread.affinity(migrate_to);
}
