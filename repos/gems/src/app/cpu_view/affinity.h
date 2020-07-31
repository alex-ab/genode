/*
 * \brief  Affinity dialog
 * \author Alexander Boettcher
 * \date   2020-07-22
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _AFFINITY_H_
#define _AFFINITY_H_

/* Genode includes */
#include <util/misc_math.h>
#include <base/affinity.h>
#include <os/reporter.h>
#include <util/noncopyable.h>

namespace Cpu {
	struct Affinity_dialog;
	class Component;

	using Genode::Affinity;
	using Genode::Noncopyable;
	using Genode::Xml_generator;
	using Genode::String;
	using Genode::size_t;

	template <typename FN>
	static inline void gen_named_node(Xml_generator &xml, char const *type,
	                                  char const *name, FN const &fn)
	{
		xml.node(type, [&] () {
			xml.attribute("name", name);
			fn();
		});
	}

	template <size_t N, typename FN>
	static inline void gen_named_node(Xml_generator &xml, char const *type,
	                                  String<N> const &name, FN const &fn)
	{
		gen_named_node(xml, type, name.string(), fn);
	}
}

struct Cpu::Affinity_dialog : Noncopyable
{
	Affinity::Space    const _space;
	Affinity::Location const _location;
	Affinity::Location       _local;

	Affinity_dialog(Affinity::Space space,
	                Affinity::Location loc,
	                Affinity::Location local)
	: _space(space), _location(loc), _local(local)
	{ }

	void _gen_affinity_entry(Xml_generator &) const;

	template <typename FUNC>
	void click(String<12> const & clicked_location, FUNC const &fn)
	{
		if (!clicked_location.valid())
			return;

		for (unsigned y = 0; y < _space.height(); y++) {
			for (unsigned x = 0; x < _space.width(); x++) {
				String<12> const name_cpu("cpu", x, "x", y);
				if (name_cpu != clicked_location)
					continue;

				if (x < unsigned(_location.xpos()) || y < unsigned(_location.ypos()))
					break;
				if (x >= _location.xpos() + _location.width())
					break;
				if (y >= _location.ypos() + _location.height())
					break;

				fn(Affinity::Location(x - _location.xpos(),
				                      y - _location.ypos(), 1, 1));
				break;
			}
		}
	}
};

#endif /* _AFFINITY_H_ */
