/*
 * \author Alexander Boettcher
 * \date   2020-10-08
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _POLICY_H_
#define _POLICY_H_

#include <base/affinity.h>
#include <base/output.h>

#include "trace.h"

namespace Cpu
{
	typedef Genode::Affinity::Location Location;
	typedef Genode::Trace::Execution_time Execution_time;
	typedef Genode::Cpu_session::Name Name;

	class Policy;
	class Policy_none;
	class Policy_pin;
	class Policy_round_robin;
	class Policy_max_utilize;
};

class Cpu::Policy {

	protected:

		bool _update(Location const &base, Location &current)
		{
			Location now = Location(base.xpos() + location.xpos(),
			                        base.ypos() + location.ypos());

			if ((now.xpos() == current.xpos()) && (now.ypos() == current.ypos()))
				return false;

			if (current.xpos() < base.xpos() || current.ypos() < base.ypos()) {
				Genode::error("affinity location strange, current below base");
				return false;
			}

			unsigned const xpos = current.xpos() - base.xpos();
			unsigned const ypos = current.ypos() - base.ypos();

			if (xpos >= base.width() || ypos >= base.height()) {
				Genode::error("affinity dimension raised");
				return false;
			}

			location = Location(xpos, ypos);

			return true;
		}

	public:

		Location location { };

		virtual ~Policy() { }
		virtual bool report(Location const &, Location const &) const = 0;
		virtual void config(Location const &) = 0;
		virtual bool update(Location const &, Location &) = 0;
		virtual void thread_create(Location const &) = 0;
		virtual bool migrate(Location const &, Location &, Trace *) const = 0;

		virtual void print(Genode::Output &output) const = 0;

		virtual bool same_type(Name const &) const = 0;
		virtual char const * string() const = 0;
};

class Cpu::Policy_none : public Cpu::Policy
{
	public:

		bool report(Location const &, Location const &) const override {
			return false; }

		void config(Location const &) override { };
		void thread_create(Location const &loc) override { location = loc; }
		bool migrate(Location const &, Location &, Trace *) const override {
			return false; }

		bool update(Location const &, Location &) override { return false; }

		void print(Genode::Output &output) const override {
			Genode::print(output, "none"); }

		bool same_type(Name const &name) const override {
			return name == "none"; }

		char const * string() const override {
			return "none"; }
};

class Cpu::Policy_pin : public Cpu::Policy
{
	public:

		bool report(Location const &loc, Location const &rel) const override {
			return (loc.xpos() != rel.xpos() || loc.ypos() != rel.ypos()); }

		void config(Location const &rel) override {
			location = rel; };

		void thread_create(Location const &loc) override {
			/* for static case with valid location, don't overwrite config */
			if (location.width() * location.height() == 0)
				location = loc;
		}

		bool migrate(Location const &base, Location &current, Trace *) const override
		{
			Location to = Location(base.xpos() + location.xpos(),
			                       base.ypos() + location.ypos());

			if ((to.xpos() == current.xpos()) && (to.ypos() == current.ypos()))
				return false;

			current = to;
			return true;
		}

		bool update(Location const &, Location &) override { return false; }

		void print(Genode::Output &output) const override {
			Genode::print(output, "pin"); }

		bool same_type(Name const &name) const override {
			return name == "pin"; }

		char const * string() const override {
			return "pin"; }
};

class Cpu::Policy_round_robin : public Cpu::Policy
{
	public:

		bool report(Location const &, Location const &) const override {
			return false; }

		void config(Location const &) override { };
		void thread_create(Location const &loc) override { location = loc; }

		bool migrate(Location const &base, Location &out, Trace *) const override
		{
			Location rel { int((location.xpos() + 1) % base.width()),
			               int( location.ypos()      % base.height()), 1, 1 }; /* XXX and what about going to y ? */

			out = Location { int(base.xpos() + rel.xpos()),
			                     base.ypos() + rel.ypos(), 1, 1 };
			return true;
		}

		bool update(Location const &base, Location &current) override {
			return _update(base, current); }

		void print(Genode::Output &output) const override {
			Genode::print(output, "round-robin"); }

		bool same_type(Name const &name) const override {
			return name == "round-robin"; }

		char const * string() const override {
			return "round-robin"; }
};

class Cpu::Policy_max_utilize : public Cpu::Policy
{
	public:

		bool report(Location const &, Location const &) const override {
			return false; }

		void config(Location const &) override { };
		void thread_create(Location const &loc) override { location = loc; }

		bool update(Location const &base, Location &current) override {
			return _update(base, current); }

		bool migrate(Location const &base, Location &current, Trace * trace) const override
		{
			if (!trace)
				return false;

			Execution_time most_idle { 0UL, 0UL };
			Location       to        { current }; /* in case of no idle info */

			for (unsigned x = base.xpos(); x < base.xpos() + base.width(); x++) {
				for (unsigned y = base.ypos(); y < base.ypos() + base.height(); y++) {

					Location       const loc(x, y);
					Execution_time const idle = trace->diff_idle_times(loc);

					if (idle.scheduling_context) {
						if (idle.scheduling_context > most_idle.scheduling_context) {
							most_idle = idle;
							to  = loc;
						}
					} else {
						if (idle.thread_context > most_idle.thread_context) {
							most_idle = idle;
							to  = loc;
						}
					}
				}
			}

			if ((to.xpos() == current.xpos()) && (to.ypos() == current.ypos()))
				return false;

			current = to;

			return true;
		}

		void print(Genode::Output &output) const override {
			Genode::print(output, "max-utilize"); }

		bool same_type(Name const &name) const override {
			return name == "max-utilize"; }

		char const * string() const override {
			return "max-utilize"; }
};

#endif
