/*
 * \brief  Data from Trace session,
 *         e.g. CPU idle times && thread execution time
 * \author Alexander Boettcher
 * \date   2020-07-22
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include "trace.h"

void Cpu::Trace::read_idle_times()
{
	if (!_trace.constructed())
		return;

	_idle_slot = (_idle_slot + 1) % HISTORY;

	for (unsigned x = 0; x < _space.width(); x++) {
		for (unsigned y = 0; y < _space.height(); y++) {
			Subject_id const &subject_id = _idle_id[x][y];
			if (!subject_id.id) {
				_lookup_missing_idle_id(Affinity::Location(x,y));
			}

			if (!subject_id.id) {
				_idle_times[x][y][_idle_slot] = Execution_time(0, 0);
				continue;
			}

			Subject_info const info = _trace->subject_info(subject_id);

			Affinity::Location location = info.affinity();

			if (location.xpos() != int(x) || location.ypos() != int(y))
				Genode::warning("location mismatch ", x, "x", y, " vs ",
				                location.xpos(), "x", location.ypos());

			_idle_times[x][y][_idle_slot] = info.execution_time();
		}
	}
}

void Cpu::Trace::_lookup_missing_idle_id(Affinity::Location const &location)
{
	bool found = false;

	auto count = _trace->for_each_subject_info([&](Subject_id const &id,
	                                               Subject_info const &info)
	{
		if (found)
			return;

		if (info.affinity().xpos() != location.xpos() ||
		    info.affinity().ypos() != location.ypos())
			return;

		if (info.session_label() != "kernel" || info.thread_name() != "idle")
			return;

		_idle_id[location.xpos()][location.ypos()] = id;

		found = true;
	});

	if (!found && count.count == count.limit)
		Genode::error("idle trace id missing && subject buffer too small");
}

Genode::Trace::Subject_id
Cpu::Trace::lookup_missing_id(Session_label const &label,
                              Thread_name const &thread)
{
	Subject_id found_id { };

	auto count = _trace->for_each_subject_info([&](Subject_id const &id,
	                                               Subject_info const &info)
	{
		if (found_id.id)
			return;

		if (label != info.session_label())
			return;

		if (thread != info.thread_name())
			return;

		found_id = id;

		Genode::error("got ", label, " ", thread, " ", id.id);
	});

	if (!found_id.id && count.count == count.limit)
		Genode::error("trace id missing && subject buffer too small");

	return found_id;
}
