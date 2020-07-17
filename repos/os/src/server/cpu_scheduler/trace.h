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

#ifndef _TRACE_H_
#define _TRACE_H_

#include <util/reconstructible.h>
#include <trace_session/connection.h>

namespace Cpu {
	class Trace;

	using Genode::Constructible;
	using Genode::Trace::Subject_id;
	using Genode::Trace::Subject_info;
	using Genode::Affinity;
	using Genode::Session_label;
	using Genode::Trace::Thread_name;
	using Genode::Trace::Connection;
	using Genode::Trace::Execution_time;
};

class Cpu::Trace
{
	private:

		Constructible<Connection> _trace { };

		Affinity::Space const _space;

		Genode::size_t _ram_quota { 11 * 4096 };
		Genode::size_t _arg_quota {  4 * 4096 };

		enum { MAX_CORES = 64, MAX_THREADS = 2, HISTORY = 4 };

		Subject_id      _idle_id   [MAX_CORES][MAX_THREADS];
		Execution_time  _idle_times[MAX_CORES][MAX_THREADS][HISTORY];
		unsigned        _idle_slot { HISTORY - 1 };

		void _lookup_missing_idle_id(Affinity::Location const &);

		void _reconstruct(Genode::Env &env)
		{
			_trace.destruct();
			_trace.construct(env, _ram_quota, _arg_quota, 0 /* parent levels */);
		}

		Affinity::Space _sanitize_space(Affinity::Space const space)
		{
			unsigned width  = space.width();
			unsigned height = space.height();

			if (_space.width() > MAX_CORES)
				width = MAX_CORES;
			if (_space.height() > MAX_THREADS)
				height = MAX_THREADS;

			if (width != space.width() || height != space.height())
				Genode::error("supported affinity space too small");

			return Affinity::Space(width, height);
		}

	public:

		Trace(Genode::Env &env)
		: _space(_sanitize_space(env.cpu().affinity_space()))
		{
			_reconstruct(env);
		}

		void read_idle_times();

		Subject_id lookup_missing_id(Session_label const &,
		                             Thread_name const &);

		template <typename FUNC>
		void retrieve(Subject_id const id, FUNC const &fn)
		{
			if (!_trace.constructed())
				return;

			/* Ieegs, XXX, avoid copying whole object */
			Subject_info info = _trace->subject_info(id);
			fn(info.execution_time(), info.affinity());
		}

		Execution_time abs_idle_times(Affinity::Location const &location)
		{
			if (location.xpos() >= MAX_CORES || location.ypos() >= MAX_THREADS)
				return Execution_time(0, 0);
			return _idle_times[location.xpos()][location.ypos()][_idle_slot];
		}

		Execution_time diff_idle_times(Affinity::Location const &location)
		{
			unsigned const xpos = location.xpos();
			unsigned const ypos = location.ypos();

			if (xpos >= MAX_CORES || ypos >= MAX_THREADS)
				return Execution_time(0, 0);

			Execution_time const &prev = _idle_times[xpos][ypos][((_idle_slot == 0) ? unsigned(HISTORY) : _idle_slot) - 1];
			Execution_time const &curr = _idle_times[location.xpos()][location.ypos()][_idle_slot];

			using Genode::uint64_t;

			uint64_t ec = (prev.thread_context < curr.thread_context) ?
			              curr.thread_context - prev.thread_context : 0;
			uint64_t sc = (prev.scheduling_context < curr.scheduling_context) ?
			              curr.scheduling_context - prev.scheduling_context : 0;

			/* strange case where idle times are not reported if no threads are on CPU */
			if (!ec && !sc && curr.thread_context == 0 && curr.scheduling_context == 0)
				return Execution_time { 1, 1 };

			return Execution_time { ec, sc };
		}
};

#endif /* _TRACE_H_ */
