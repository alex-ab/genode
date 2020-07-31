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

#include "affinity.h"

using namespace Cpu;

void Affinity_dialog::_gen_affinity_entry(Xml_generator &xml) const
{
//	gen_named_node(xml, "frame", name, [&] () {
		gen_named_node(xml, "float", "affinity_float", [&] () {
			xml.attribute("west", "yes");

			xml.node("vbox", [&] () {
				for (unsigned y = 0; y < _space.height(); y++) {
					bool const row = unsigned(_location.ypos()) <= y && y < _location.ypos() + _location.height();
					String<12> const row_id("row", y);

					gen_named_node(xml, "hbox", row_id, [&] () {
						for (unsigned x = 0; x < _space.width(); x++) {
							String<12> const name_cpu("cpu", x, "x", y);
							bool const column = unsigned(_location.xpos()) <= x && x < _location.xpos() + _location.width();
							gen_named_node(xml, "button", name_cpu, [&] () {
								if (row && column)
									xml.attribute("hovered", "yes");

								if (_location.xpos() + _local.xpos() == int(x) &&
								    _location.ypos() + _local.ypos() == int(y))
									xml.attribute("selected", "yes");
	
								xml.attribute("style", "checkbox");

								xml.node("hbox", [&] () { });
							});
						}
					});
				}
			});
		});
//	});
}
