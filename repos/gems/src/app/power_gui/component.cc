/*
 * \brief  GUI for managing power states for AMD & Intel
 * \author Alexander Boettcher
 * \date   2022-10-15
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/signal.h>

#include <os/reporter.h>

#include "xml_tools.h"
#include "button.h"

using namespace Genode;


struct State
{
	unsigned value { ~0U };

	bool valid() const { return value != ~0U; }
	void invalidate() { value = ~0U; }

	bool operator == (State const &o) const { return value == o.value; }
	bool operator != (State const &o) const { return value != o.value; }
};


class Power
{
	private:
		enum { CPU_MUL = 10000 };

		Env                    &_env;
		Attached_rom_dataspace  _info     { _env, "info" };
		Signal_handler<Power>   _info_sig { _env.ep(), *this, &Power::_info_update };

		Attached_rom_dataspace  _hover     { _env, "hover" };
		Signal_handler<Power>   _hover_sig { _env.ep(), *this, &Power::_hover_update};

		Reporter                _dialog          { _env, "dialog" };
		Reporter                _msr_config      { _env, "config" };

		State                   _setting_cpu     { };
		State                   _setting_hovered { };
		bool                    _apply_hovered   { false };

		/* ranges are set by read out hardware features */
		Button_hub<1, 0, 10, 0>    _amd_pstate { };

		/* PERFORMANCE = 0, BALANCED = 7, POWER_SAVING = 15 */
		Button_hub<1, 0, 15, 7>    _intel_epb  { };

		/* ranges are set by read out hardware features */
		Button_hub<1, 0, 255, 128> _intel_hwp_min { };
		Button_hub<1, 0, 255, 128> _intel_hwp_max { };
		Button_hub<1, 0, 255, 128> _intel_hwp_des { };

		/* PERFORMANCE = 0, BALANCED = 128, ENERGY = 255 */
		Button_hub<1, 0, 255, 128> _intel_hwp_epp { };

		void _generate_msr_config();
		void _info_update();
		void _hover_update();
		void _per_cpu(Reporter::Xml_generator &, Xml_node &);
		void _settings_view(Reporter::Xml_generator &, Xml_node &);

		template <typename T>
		void hub(Genode::Xml_generator &xml, T &hub, char const *name)
		{
			hub.for_each([&](Button_state &state, unsigned pos) {
				xml.attribute("name", Genode::String<20>("hub-", name, "-", pos));

				Genode::String<12> number(state.current);

				xml.node("button", [&] () {
					xml.attribute("name", Genode::String<20>("hub-", name, "-", pos));
					xml.node("label", [&] () {
						xml.attribute("text", number);
					});

					if (state.active())
						xml.attribute("hovered", true);
				});
			});
		}

	public:

		Power(Env &env) : _env(env)
		{
			_info.sigh(_info_sig);
			_hover.sigh(_hover_sig);
			_dialog.enabled(true);
			_msr_config.enabled(true);
		}
};


void Power::_hover_update()
{
	_hover.update();

	if (!_hover.valid())
		return;

	Genode::Xml_node const hover = _hover.xml();

	typedef Genode::String<20> Button;
	Button button = query_attribute<Button>(hover, "dialog", "frame",
	                                        "vbox", "hbox", "button", "name");
	if (button == "") /* intel hwp, epb, epp & AMD pstate */
		button = query_attribute<Button>(hover, "dialog", "frame",
	                                     "vbox", "vbox", "hbox", "button", "name");

	if (button == "") /* apply button */
		button = query_attribute<Button>(hover, "dialog", "frame",
		                                 "vbox", "vbox", "button", "name");

	bool click_valid = false;
	Button click = query_attribute<Button>(hover, "button", "left");
	if (click == "yes") {
		click = "left";
		click_valid = true;
	} else {
		click = query_attribute<Button>(hover, "button", "right");
		if (click == "yes") {
			click = "right";
			click_valid = true;
		} else {
/*
			long y = query_attribute<long>(hover, "button", "wheel");
			click_valid = y;
			if (y < 0) click = "wheel_down";
			if (y > 0) click = "wheel_up";
*/
		}
	}

	bool refresh = false;

	if (click_valid && _setting_hovered.valid()) {
		if (_setting_cpu == _setting_hovered) {
			_setting_cpu.invalidate();
		} else
			_setting_cpu = _setting_hovered;

		refresh = true;
	}

	if (click_valid && _apply_hovered) {
		_generate_msr_config();
		_setting_cpu.invalidate();
		refresh = true;
	}

	if (click_valid && _setting_cpu.valid()) {
		if (_amd_pstate.any_active()) {
			if (click == "left")
				refresh = refresh || _amd_pstate.update_inc();
			else
			if (click == "right")
				refresh = refresh || _amd_pstate.update_dec();
		}

		if (_intel_epb.any_active()) {
			if (click == "left")
				refresh = refresh || _intel_epb.update_inc();
			else
			if (click == "right")
				refresh = refresh || _intel_epb.update_dec();
		}

		if (_intel_hwp_min.any_active()) {
			if (click == "left")
				refresh = refresh || _intel_hwp_min.update_inc();
			else
			if (click == "right")
				refresh = refresh || _intel_hwp_min.update_dec();
		}

		if (_intel_hwp_max.any_active()) {
			if (click == "left")
				refresh = refresh || _intel_hwp_max.update_inc();
			else
			if (click == "right")
				refresh = refresh || _intel_hwp_max.update_dec();
		}

		if (_intel_hwp_des.any_active()) {
			if (click == "left")
				refresh = refresh || _intel_hwp_des.update_inc();
			else
			if (click == "right")
				refresh = refresh || _intel_hwp_des.update_dec();
		}

		if (_intel_hwp_epp.any_active()) {
			if (click == "left")
				refresh = refresh || _intel_hwp_epp.update_inc();
			else
			if (click == "right")
				refresh = refresh || _intel_hwp_epp.update_dec();
		}
	}

	if (click_valid) {
		if (refresh)
			_info_update();
		return;
	}

	auto const before_hovered = _setting_hovered;
	auto const before_cpu     = _setting_cpu;
	auto const before_pstate  = _amd_pstate.any_active();
	auto const before_epb     = _intel_epb.any_active();
	auto const before_hwp_min = _intel_hwp_min.any_active();
	auto const before_hwp_max = _intel_hwp_max.any_active();
	auto const before_hwp_des = _intel_hwp_des.any_active();
	auto const before_hwp_epp = _intel_hwp_epp.any_active();
	auto const before_apply   = _apply_hovered;

	bool const any = button != "";

	bool const hovered_setting = any && (button == "settings");
	bool const hovered_pstate  = any && (String<11>(button) == "hub-pstate");
	bool const hovered_epb     = any && (String< 8>(button) == "hub-epb");
	bool const hovered_hwp_min = any && (String<12>(button) == "hub-hwp_min");
	bool const hovered_hwp_max = any && (String<12>(button) == "hub-hwp_max");
	bool const hovered_hwp_des = any && (String<12>(button) == "hub-hwp_des");
	bool const hovered_hwp_epp = any && (String<12>(button) == "hub-hwp_epp");

	_apply_hovered = any && (button == "apply");

	if (hovered_setting) {
		_setting_hovered.value = query_attribute<unsigned>(hover, "dialog", "frame",
		                                                   "vbox", "hbox", "name");
	} else if (_setting_hovered.valid())
		_setting_hovered.invalidate();

	_amd_pstate.for_each([&](Button_state &state, unsigned) {
		state.hovered = hovered_pstate;
	});

	_intel_epb.for_each([&](Button_state &state, unsigned) {
		state.hovered = hovered_epb;
	});

	_intel_hwp_min.for_each([&](Button_state &state, unsigned) {
		state.hovered = hovered_hwp_min;
	});

	_intel_hwp_max.for_each([&](Button_state &state, unsigned) {
		state.hovered = hovered_hwp_max;
	});

	_intel_hwp_des.for_each([&](Button_state &state, unsigned) {
		state.hovered = hovered_hwp_des;
	});

	_intel_hwp_epp.for_each([&](Button_state &state, unsigned) {
		state.hovered = hovered_hwp_epp;
	});

	if ((before_hovered != _setting_hovered) ||
	    (before_cpu     != _setting_cpu)     ||
	    (before_pstate  != hovered_pstate)  ||
	    (before_epb     != hovered_epb) ||
	    (before_hwp_min != hovered_hwp_min) ||
	    (before_hwp_max != hovered_hwp_max) ||
	    (before_hwp_des != hovered_hwp_des) ||
	    (before_hwp_epp != hovered_hwp_epp) ||
	    (before_apply   != _apply_hovered))
		refresh = true;

	if (refresh)
		_info_update();
}


void Power::_info_update ()
{
	_info.update();

	if (!_info.valid())
		return;

	Reporter::Xml_generator xml(_dialog, [&] () {
		xml.node("frame", [&] {
			xml.node("vbox", [&] {
				_info.xml().for_each_sub_node("cpu", [&](Genode::Xml_node &cpu) {
					_per_cpu(xml, cpu);
				});
			});
		});
	});
}


void Power::_generate_msr_config()
{
	if (!_setting_cpu.valid())
		return;

	auto const affinity_x = _setting_cpu.value / CPU_MUL;
	auto const affinity_y = _setting_cpu.value % CPU_MUL;

	Reporter::Xml_generator xml(_msr_config, [&] () {
		xml.attribute("verbose", false);

		xml.node("cpu", [&] {
			xml.attribute("x", affinity_x);
			xml.attribute("y", affinity_y);

			xml.node("pstate", [&] {
				xml.attribute("rw_command", _amd_pstate.value());
			});

			xml.node("hwp_request", [&] {
				xml.attribute("min",     _intel_hwp_min.value());
				xml.attribute("max",     _intel_hwp_max.value());
				xml.attribute("desired", _intel_hwp_des.value());
				xml.attribute("epp",     _intel_hwp_epp.value());
			});

			xml.node("intel_speed_step", [&] {
				xml.attribute("epb", _intel_epb.value());
			});
		});
	});
}


void Power::_per_cpu(Reporter::Xml_generator &xml, Xml_node &cpu)
{
	auto const affinity_x = cpu.attribute_value("x", 0U);
	auto const affinity_y = cpu.attribute_value("y", 0U);
	auto const freq_khz   = cpu.attribute_value("freq_khz", 0ULL);
	auto const temp_c     = cpu.attribute_value("temp_c", 0U);

	auto const cpu_id     = affinity_x * CPU_MUL + affinity_y;

	xml.node("hbox", [&] {
		auto const name = String<12>("CPU ", affinity_x, "x", affinity_y);

		xml.attribute("name", cpu_id);

		xml.node("label", [&] {
			xml.attribute("name", 1);
			xml.attribute("text", name);
		});

		xml.node("label", [&] {
			xml.attribute("name", 2);
			xml.attribute("text", String<12>(" - ", temp_c, " °C - "));
		});

		xml.node("label", [&] {
			xml.attribute("name", 3);
			xml.attribute("text", String<16>(freq_khz / 1000, ".",
			                                 (freq_khz % 1000) / 10, " MHz"));
		});

		xml.node("button", [&] () {
			xml.attribute("name", "settings");
			xml.node("label", [&] () {
				xml.attribute("text", "");
			});

			if (_setting_hovered.value == cpu_id)
				xml.attribute("hovered", true);
		});
	});

	if (cpu_id != _setting_cpu.value)
		return;

	xml.node("vbox", [&] {
		_settings_view(xml, cpu);
	});
}


void Power::_settings_view(Reporter::Xml_generator &xml, Xml_node &cpu)
{
	xml.attribute("name", "settings");

	unsigned hwp_high = 0;
	unsigned hwp_low  = 0;

	cpu.for_each_sub_node([&](Genode::Xml_node &node) {

		if (node.type() == "pstate") {
			unsigned min = node.attribute_value("ro_limit_cur", 0u);
			unsigned max = node.attribute_value("ro_max_value", 0u);
			unsigned cur = node.attribute_value("ro_status", 0u);

			_amd_pstate.set_min_max(min, max);

			xml.node("hbox", [&] () {
				xml.attribute("name", "pstate");

				xml.node("label", [&] () {
					xml.attribute("text", String<32>("pstate: [", min, "-", max, "] current=", cur));
				});

				hub(xml, _amd_pstate, "pstate");
			});

			return;
		}

		if (node.type() == "intel_speed_step" && node.has_attribute("epb")) {
			unsigned epb = node.attribute_value("epb", 0);

			xml.node("hbox", [&] () {
				xml.attribute("name", "epb");

				auto text = String<64>("Intel speed step: [", _intel_epb.min(),
				                       "-", _intel_epb.max(), "] current=", epb);
				xml.node("label", [&] () {
					xml.attribute("text", text);
				});

				hub(xml, _intel_epb, "epb");
			});

			xml.node("hbox", [&] () {
				xml.attribute("name", "epbhints");
				xml.node("label", [&] () {
					xml.attribute("text", "PERFORMANCE = 0, BALANCED = 7, POWER_SAVING = 15");
				});
			});

			return;
		}

		if (node.type() == "hwp_cap") {
			unsigned effi = node.attribute_value("effi" , 1);
			unsigned guar = node.attribute_value("guar" , 1);

			hwp_high = node.attribute_value("high" , 0);
			hwp_low  = node.attribute_value("low"  , 0);

			xml.node("hbox", [&] () {
				xml.attribute("name", "hwpcap");

				auto text = String<64>("Intel HWP features: [", hwp_low, "-",
				                       hwp_high, "] efficient=", effi,
				                       " guaranty=", guar);
				xml.node("label", [&] () {
					xml.attribute("text", text);
				});
			});

			return;
		}

		if (node.type() == "hwp_request") {
			unsigned min = node.attribute_value("min"     , 1);
			unsigned max = node.attribute_value("max"     , 1);
			unsigned des = node.attribute_value("desired" , 1);
			unsigned epp = node.attribute_value("epp"     , 1);

			if (hwp_low && hwp_high && (_intel_hwp_min.min() < hwp_low ||
			                            _intel_hwp_min.max() > hwp_high))
			{
				_intel_hwp_min.set_min_max(hwp_low, hwp_high);
				_intel_hwp_max.set_min_max(hwp_low, hwp_high);
				_intel_hwp_des.set_min_max(hwp_low, hwp_high);

				/* read out features sometimes are not within hw range .oO */
				if (hwp_low <= min && min <= hwp_high)
					_intel_hwp_min.set(min);
				if (hwp_low <= max && max <= hwp_high)
					_intel_hwp_max.set(max);
				if (hwp_low <= des && des <= hwp_high)
					_intel_hwp_des.set(des);

				_intel_hwp_epp.set(epp);
			}

			xml.node("hbox", [&] () {
				xml.attribute("name", "hwpreq");

				xml.node("label", [&] () {
					xml.attribute("name", 2);
					xml.attribute("text", String<16>("HWP min: ", min));
				});
				hub(xml, _intel_hwp_min, "hwp_min");

				xml.node("label", [&] () {
					xml.attribute("name", 3);
					xml.attribute("text", String<16>("max: ", max));
				});
				hub(xml, _intel_hwp_max, "hwp_max");

				xml.node("label", [&] () {
					xml.attribute("name", 4);
					xml.attribute("text", String<16>("desired: ", des));
				});
				hub(xml, _intel_hwp_des, "hwp_des");
			});

			xml.node("hbox", [&] () {
				xml.attribute("name", "hwpepp");

				auto text = String<64>("Intel EPP: [", _intel_hwp_epp.min(),
				                       "-", _intel_hwp_epp.max(), "] current=", epp);
				xml.node("label", [&] () {
					xml.attribute("text", text);
				});

				hub(xml, _intel_hwp_epp, "hwp_epp");
			});

			xml.node("hbox", [&] () {
				xml.attribute("name", "hwpepphints");
				xml.node("label", [&] () {
					xml.attribute("text", "PERFORMANCE = 0, BALANCED = 128, ENERGY = 255");
				});
			});

			return;
		}

		if (node.type() == "hwp_coord_feed_cap") {
			xml.node("hbox", [&] () {
				xml.attribute("name", "hwpcoord");

				auto text = String<64>("Intel HWP coordination feedback "
				                       "available but not supported.");
				xml.node("label", [&] () {
					xml.attribute("text", text);
				});
			});

			return;
		}
	});

	xml.node("button", [&] () {
		xml.attribute("name", "apply");
		xml.node("label", [&] () {
			xml.attribute("text", "apply");
		});

		if (_apply_hovered)
			xml.attribute("hovered", true);
	});
}

void Component::construct(Genode::Env &env) { static Power state(env); }
