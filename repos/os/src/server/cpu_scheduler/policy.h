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

namespace Cpu
{
	typedef Genode::Affinity::Location Location;

	class Policy;
	class Policy_none;
	class Policy_pin;
	class Policy_round_robin;
	class Policy_max_utilize;
};

class Cpu::Policy {

	public:

		virtual ~Policy() { }
		virtual bool report(Location const &, Location const &) const = 0;
		virtual void config(Location &, Location const &) const = 0;
};

class Cpu::Policy_none : public Cpu::Policy
{
	public:

		bool report(Location const &, Location const &) const override {
			return false; }

		void config(Location &, Location const &) const override { };
};

class Cpu::Policy_pin : public Cpu::Policy
{
	public:

		bool report(Location const &loc, Location const &rel) const override {
			return (loc.xpos() != rel.xpos() || loc.ypos() != rel.ypos()); }

		void config(Location &loc, Location const &rel) const override {
			loc = rel; };
};

class Cpu::Policy_round_robin : public Cpu::Policy
{
	public:

		bool report(Location const &, Location const &) const override {
			return false; }

		void config(Location &, Location const &) const override { };
};

class Cpu::Policy_max_utilize : public Cpu::Policy
{
	public:

		bool report(Location const &, Location const &) const override {
			return false; }

		void config(Location &, Location const &) const override { };
};

#endif
