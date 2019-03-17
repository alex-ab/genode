/*
 * \brief  GUI for battery information using Menu_view
 * \author Alexander Boettcher
 * \date   2019-03-17
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/signal.h>

#include <os/reporter.h>

#include "xml_tools.h"

using Genode::Attached_rom_dataspace;
using Genode::Signal_handler;
using Genode::Reporter;

class Battery
{
	private:

		Genode::Env             &_env;

		Attached_rom_dataspace   _battery        { _env, "acpi_battery" };
		Signal_handler<Battery>  _battery_signal { _env.ep(), *this, &Battery::_battery_update};

		Attached_rom_dataspace   _hover          { _env, "hover" };
		Signal_handler<Battery>  _hover_signal   { _env.ep(), *this, &Battery::_hover_update};

		Reporter                 _dialog         { _env, "dialog" };
		bool                     _more_hovered   { false };
		bool                     _more_view      { false };
		Genode::uint8_t          _more_cnt       { 0 };

		void _battery_update();
		void _hover_update();

		Genode::String<8> _percent(unsigned percent, unsigned rest) {
			return Genode::String<8> (percent < 10 ? "  " : (percent < 100 ? " " : ""),
			                          percent, ".", rest < 10 ? "0" : "", rest, "%");
		}

	public:

		Battery(Genode::Env &env)
		: _env(env)
		{
			_battery.sigh(_battery_signal);
			_hover.sigh(_hover_signal);
			_dialog.enabled(true);
		}
};

void Battery::_hover_update()
{
	_hover.update();

	if (!_hover.valid())
		return;

	Genode::Xml_node const hover = _hover.xml();

	typedef Genode::String<12> Button;
	Button button = query_attribute<Button>(hover, "dialog", "frame",
	                                        "vbox", "hbox", "button", "name");
	Button const click = query_attribute<Button>(hover, "button", "left");

	if (click == "yes" && (_more_hovered || _more_view)) {
		_more_view = !_more_view;

		_battery_update();
	}

	_more_hovered = button == "more";
	if (_more_hovered)
		_more_cnt = query_attribute<long>(hover, "dialog", "frame",
		                                 "vbox", "hbox", "name");
}

void Battery::_battery_update()
{
	_battery.update();

	if (!_battery.valid())
		return;

	unsigned cnt = 0;

	Reporter::Xml_generator xml(_dialog, [&] () {
		xml.node("frame", [&] {
			xml.node("vbox", [&] {
				_battery.xml().for_each_sub_node("sb", [&](Genode::Xml_node &nodex) {
					cnt++;
					typedef unsigned long Value;
					Value d = query_attribute<Value>(nodex, "design_capacity", "value");
					Value l = query_attribute<Value>(nodex, "last_full_capacity", "value");
					Value r = query_attribute<Value>(nodex, "remaining_capacity", "value");
					Value w = query_attribute<Value>(nodex, "warning_capacity", "value");
					Value s = query_attribute<Value>(nodex, "state", "value");

					/* take care of missing values, e.g. broken battery information */
					if (d == 0) d = 100;
					if (l == 0) l = d;
					if (r == 0) r = 1;
					if (w == 0 || w >= l) w = l / 10;

					/* if battery recovers, the remaining capacity seems to exceed last full capacity */
					if (r > l) l = r;

					unsigned percent = r * 100 / l;
					unsigned rest = r * 10000 / l - (percent * 100);

					xml.node("hbox", [&] {
						xml.attribute("name", cnt);
						try {
							xml.node("label", [&] {
								Genode::Xml_node node = nodex.sub_node("name");
								Genode::String<32> text = node.decoded_content<Genode::String<32>>();

								xml.attribute("text", Genode::String<32>(text, " "));
							});
						} catch (...) { /* missing name ... */ }

						xml.node("float", [&] () {
							xml.node("bar", [&] () {
								if (r > w) {
									if (s == 1 /* discharging */) {
										xml.attribute("color", "#ffff00");
										xml.attribute("textcolor", "#000000");
									} else {
										xml.attribute("color", "#0ff000");
										xml.attribute("textcolor", "#000000");
									}
								} else {
									/* below warning value */
									xml.attribute("color", "#ff0000");
									xml.attribute("textcolor", "#ff0000");
								}

								xml.attribute("percent", percent);
								xml.attribute("width", 96);
								xml.attribute("height", 24);
								xml.attribute("text", _percent(percent, rest));
							});
						});

						xml.node("button", [&] () {
							xml.attribute("name", "more");
							xml.node("label", [&] () {
								xml.attribute("text", "");
							});
						});
					});

					if (!_more_view || _more_cnt != cnt)
						return;

					xml.node("vbox", [&] {
						xml.attribute("name", "details");
						nodex.for_each_sub_node([&](Genode::Xml_node &node) {
							xml.node("label", [&] {
								xml.attribute("align", "left");
								xml.attribute("name", node.type());

								Genode::String<32> value;
								if (node.has_attribute("value")) {
									unsigned long data = 0;
									node.attribute("value").value(data);
									value = Genode::String<32>("value=", data);
								}
								Genode::String<32> text = node.decoded_content<Genode::String<32>>();

								xml.attribute("text", Genode::String<128>(node.type(), " ", value, " content=", text));
							});
						});
					});
				});
			});
		});
	});

}

void Component::construct(Genode::Env &env) { static Battery state(env); }
