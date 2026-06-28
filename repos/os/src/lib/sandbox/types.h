/*
 * \brief  Common types used within init
 * \author Norman Feske
 * \date   2017-03-03
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIB__SANDBOX__TYPES_H_
#define _LIB__SANDBOX__TYPES_H_

#include <util/list.h>
#include <base/node.h>
#include <pd_session/pd_session.h>

namespace Sandbox {

	class Child;

	using namespace Genode;
	using Genode::size_t;
	using Genode::strlen;

	struct Prio_levels { long value; };

	struct Cpu_quota
	{
		unsigned percent;

		void print(Output &out) const { Genode::print(out, percent, "%"); }
	};

	using Child_list = List<List_element<Child> >;

	template <typename T>
	struct Resource_info
	{
		T quota, used, avail;

		static Resource_info from_pd_stats(Pd_session::Stats const &stats);

		void generate(Generator &g) const
		{
			using Value = String<32>;
			g.attribute("quota", Value(quota));
			g.attribute("used",  Value(used));
			g.attribute("avail", Value(avail));
		}

		bool operator != (Resource_info const &other) const
		{
			return quota.value != other.quota.value
			    || used.value  != other.used.value
			    || avail.value != other.avail.value;
		}
	};

	using Ram_info = Resource_info<Ram_quota>;
	using Cap_info = Resource_info<Cap_quota>;

	template <>
	inline Ram_info Ram_info::from_pd_stats(Pd_session::Stats const &stats)
	{
		return { .quota = stats.ram.limit,
		         .used  = stats.ram.used,
		         .avail = stats.ram.avail() };
	}

	template <>
	inline Cap_info Cap_info::from_pd_stats(Pd_session::Stats const &stats)
	{
		return { .quota = stats.caps.limit,
		         .used  = stats.caps.used,
		         .avail = stats.caps.avail() };
	}

	struct Default_quota    { Ram_quota ram; Cap_quota caps; };
	struct Configured_quota { Ram_quota ram; Cap_quota caps; };
	struct Assigned_quota   { Ram_quota ram; Cap_quota caps; };
	struct Preserved_quota  { Ram_quota ram; Cap_quota caps; };
	struct Quota_limit      { Ram_quota ram; Cap_quota caps; };

	struct Preservation : private Noncopyable
	{
		static Ram_quota default_ram()  { return { 40*sizeof(long)*1024 }; }
		static Cap_quota default_caps() { return { 20 }; }

		Ram_quota ram  { };
		Cap_quota caps { };

		void reset()
		{
			ram  = default_ram();
			caps = default_caps();
		}

		Preservation() { reset(); }
	};
}

#endif /* _LIB__SANDBOX__TYPES_H_ */
