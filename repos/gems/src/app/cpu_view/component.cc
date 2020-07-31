/*
 * \author Alexander Boettcher
 * \date   2020-07-31
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/signal.h>
#include <os/reporter.h>

#include "affinity.h"

namespace Cpu {
	struct View;

	using Genode::Affinity;
	using Genode::Attached_rom_dataspace;
	using Genode::Constructible;
	using Genode::Reporter;
	using Genode::Session_label;
	using Genode::Signal_handler;
	using Genode::Thread;
	using Genode::Xml_node;
	using Genode::uint8_t;
}

template <typename EXC, typename T, typename FUNC, typename HANDLER>
auto retry(T &env, FUNC func, HANDLER handler,
           unsigned attempts = ~0U) -> decltype(func())
{
	try {
		for (unsigned i = 0; attempts == ~0U || i < attempts; i++)
			try { return func(); }
			catch (EXC) {
				if ((i + 1) % 5 == 0 || env.pd().avail_ram().value < 8192)
					Genode::warning(i, ". attempt to extend dialog report "
					                "size, ram_avail=", env.pd().avail_ram());

				if (env.pd().avail_ram().value < 8192)
					throw;

				handler();
			}

		throw EXC();
	} catch (Genode::Xml_generator::Buffer_exceeded) {
		Genode::error("not enough memory for xml");
	}
	return;
}

struct Cpu::View
{
	Genode::Env            &_env;
	Attached_rom_dataspace  _config     { _env, "config" };
	Attached_rom_dataspace  _components { _env, "components" };
	Attached_rom_dataspace  _hover      { _env, "hover" };
	Constructible<Reporter> _dialog     { };
	Constructible<Reporter> _cpu_config { };
	Affinity::Space const   _space      { _env.cpu().affinity_space() };

	Signal_handler<View> signal_config {
		_env.ep(), *this, &View::handle_config };

	Signal_handler<View> signal_components {
		_env.ep(), *this, &View::handle_components };

	Signal_handler<View> signal_hover {
		_env.ep(), *this, &View::handle_hover };

	void handle_config();
	void handle_components();
	void _handle_components();
	void handle_hover();

	enum POLICY {
		POLICY_UNKNOWN = 0,
		POLICY_NONE,
		POLICY_PIN,
		POLICY_ROUND_ROBIN,
		POLICY_MAX_UTILIZE,
	};

	enum POLICY string_to_policy(Thread::Name const &name)
	{
		if (name == "pin")
			return POLICY_PIN;
		else if (name == "round-robin")
			return POLICY_ROUND_ROBIN;
		else if (name == "max-utilize")
			return POLICY_MAX_UTILIZE;
		else
			return POLICY_NONE;
	}

	static char const * policy_to_string(enum POLICY const policy)
	{
		switch (policy) {
		case POLICY_PIN:         return "pin";
		case POLICY_ROUND_ROBIN: return "round-robin";
		case POLICY_MAX_UTILIZE: return "max-utilize";
		case POLICY_NONE:        return "none";
		case POLICY_UNKNOWN:     return "unknown";
		}
		return "none";
	}

	static char const * policy_to_short(enum POLICY const policy)
	{
		switch (policy) {
		case POLICY_PIN:         return "PIN";
		case POLICY_ROUND_ROBIN: return "ROB";
		case POLICY_MAX_UTILIZE: return "MAX";
		case POLICY_NONE:        return "NON";
		case POLICY_UNKNOWN:     return "UNK";
		}
		return "none";
	}

	POLICY   _default_policy   { POLICY_UNKNOWN };

	unsigned _dialog_size      { 4096 * 2 };
	unsigned _config_size      { 4096 * 2 };

	unsigned hover_component    { 0 };
	unsigned selected_component { 0 };
	unsigned hover_thread       { 0 };
	unsigned selected_thread    { 0 };

	bool    back_component     { false };
	bool    back_thread        { false };
	bool    _hover_confirm     { false };
	bool    affinity_hover     { false };
	bool    affinity_iconize   { false };
	bool    default_policy_hover { false };

	struct {
		uint8_t            previous    { POLICY_UNKNOWN };
		uint8_t            selected    { POLICY_UNKNOWN };
		uint8_t            hovered     { POLICY_UNKNOWN };
		Affinity::Location pin_loc     { 0, 0, 1, 1 };
		Affinity::Location pin         { 0, 0, 1, 1 };
		String<12>         pin_hovered { };

		void reset()
		{
			hovered     = selected = previous = POLICY_UNKNOWN;
			pin         = Affinity::Location (0, 0, 1, 1);
			pin_loc     = Affinity::Location (0, 0, 1, 1);
			pin_hovered = String<10>();
		}
	} _policy { };

	void gen_dialog(Reporter::Xml_generator &, Xml_node const &);
	void gen_config(Reporter::Xml_generator &, Xml_node const &);
	void _gen_menu_entry(Xml_generator &, unsigned,
	                     Session_label const &, bool,
	                     char const *style = "radio") const;
	void _gen_back_entry(Xml_generator &, Session_label const &) const;

	View(Genode::Env &env) : _env(env)
	{
		_config.sigh(signal_config);
		_components.sigh(signal_components);
		_hover.sigh(signal_hover);

		Genode::log("view affinity space=",
		            _space.width(), "x", _space.height());

		handle_config();
	}
};

void Cpu::View::handle_config()
{
	_config.update();

	if (!_config.valid())
		return;

	_dialog.construct(_env, "dialog", "dialog", _dialog_size);
	_dialog->enabled(true);

	_cpu_config.construct(_env, "config", "components", _config_size);
	_cpu_config->enabled(true);
}

void Cpu::View::handle_components()
{
	_components.update();

	_handle_components();
}

void Cpu::View::_handle_components()
{
	retry<Genode::Xml_generator::Buffer_exceeded>(_env,
		[&] () {
			Reporter::Xml_generator xml(*_dialog, [&] () {
				xml.node("frame", [&] () {
					xml.node("vbox", [&] () {
						if (_components.valid())
							gen_dialog(xml, _components.xml());
						else {
							xml.node("label", [&] () {
								xml.attribute("text", "No CPU sessions yet.");
							});
						}
					});
				});
			});
		}, [&] () {
			_dialog_size += 4096;
			_dialog.construct(_env, "dialog", "dialog", _dialog_size);
			_dialog->enabled(true);
		});
}

template <typename T>
static T _attribute_value(Genode::Xml_node node, char const *attr_name)
{
	return node.attribute_value(attr_name, T{});
}

template <typename T, typename... ARGS>
static T _attribute_value(Genode::Xml_node node, char const *sub_node_type, ARGS... args)
{
	if (!node.has_sub_node(sub_node_type))
		return T{};

	return _attribute_value<T>(node.sub_node(sub_node_type), args...);
}

/**
 * Query attribute value from XML sub node
 *
 * The list of arguments except for the last one refer to XML path into the
 * XML structure. The last argument denotes the queried attribute name.
 */
template <typename T, typename... ARGS>
static T query_attribute(Genode::Xml_node node, ARGS &&... args)
{
	return _attribute_value<T>(node, args...);
}

void Cpu::View::handle_hover()
{
	_hover.update();

	if (!_hover.valid())
		return;

	Xml_node const node = _hover.xml();

	typedef String<12> Button;
	Button click = query_attribute<Button>(node, "button", "left");
	if (click == "yes") {
		bool generate_config = false;

		if (back_component) {
			if (selected_thread && selected_component)
				selected_thread = hover_thread = 0;
			else {
				hover_thread = hover_component = 0;
				selected_thread = selected_component = 0;
			}

			_policy.reset();
			_hover_confirm = false;

			back_component = false;
		}

		if (affinity_hover)
			affinity_iconize = !affinity_iconize;

		if (default_policy_hover) {
			switch (_default_policy) {
			case POLICY_PIN:         _default_policy = POLICY_ROUND_ROBIN; break;
			case POLICY_ROUND_ROBIN: _default_policy = POLICY_MAX_UTILIZE; break;
			case POLICY_MAX_UTILIZE: _default_policy = POLICY_NONE; break;
			case POLICY_NONE:        _default_policy = POLICY_ROUND_ROBIN; break;
			case POLICY_UNKNOWN:     _default_policy = POLICY_NONE; break;
			}
			generate_config = true;
		}

		if (selected_thread && selected_component) {
			if (_policy.selected == POLICY_PIN) {
				Cpu::Affinity_dialog dialog { _space, _policy.pin_loc,
				                              _policy.pin };
				dialog.click(_policy.pin_hovered, [&] (Affinity::Location loc) {
					_policy.pin = loc;
				});
			}

			if (_policy.hovered)
				_policy.selected = _policy.hovered;

			if (_hover_confirm && _policy.selected) {
				_policy.previous = _policy.selected;

				generate_config = true;
			}
		}

		if (generate_config) {
			retry<Genode::Xml_generator::Buffer_exceeded>(_env,
				[&] () {
					Reporter::Xml_generator xml(*_cpu_config, [&] () {
						gen_config(xml, _components.xml());
					});
				}, [&] () {
					_config_size += 4096;
					_cpu_config.construct(_env, "config", "components", _config_size);
					_cpu_config->enabled(true);
				});
		}

		if (hover_thread && selected_component)
			selected_thread = hover_thread;

		if (hover_component && !selected_component) {
			selected_component = hover_component;
			_default_policy = POLICY_UNKNOWN;
		}

		_handle_components();
		return;
	}

	if (!selected_thread && !selected_component) {
		hover_component = query_attribute<unsigned>(node, "dialog", "frame",
		                                           "vbox", "hbox", "name");
		return;
	}

	if (!selected_thread && selected_component) {
		typedef String<8> Button;
		Button button = query_attribute<Button>(node, "dialog", "frame",
		                                       "vbox", "hbox", "hbox", "name");

		if (button == "back") {
			back_component = true;
		} else
			back_component = false;

		button = query_attribute<Button>(node, "dialog", "frame",
		                                "vbox", "hbox", "button", "name");
		affinity_hover = button == "iconize";
		default_policy_hover = button == "default";

		hover_thread = query_attribute<unsigned>(node, "dialog", "frame",
		                                         "vbox", "vbox", "hbox", "name");
		return;
	}

	if (selected_thread && selected_component) {
		typedef String<8> Back;
		Back const back = query_attribute<Back>(node, "dialog", "frame",
		                                        "vbox", "vbox", "hbox", "float",
		                                        "hbox", "hbox", "name");
		if (back == "back") {
			back_component = true;
		} else
			back_component = false;

		_policy.hovered = query_attribute<uint8_t>(node, "dialog", "frame",
		                                          "vbox", "vbox", "hbox", "vbox", "hbox", "name");

		if (_policy.selected == POLICY_PIN) {
			typedef String<12> Loc;
			Loc cpu = query_attribute<Loc>(node, "dialog", "frame",
			                               "vbox", "vbox", "hbox", "vbox",
			                               "float", "vbox", "hbox",
			                               "button", "name");
			_policy.pin_hovered = cpu;
		}

		Back const confirm = query_attribute<Back>(node, "dialog", "frame",
		                                           "vbox", "vbox", "hbox", "name");
		if (confirm == "confirm")
			_hover_confirm = true;
		else
			_hover_confirm = false;
		return;
	}
}

void Cpu::View::_gen_menu_entry(Xml_generator &xml, unsigned const id,
                                Session_label const &text, bool selected,
                                char const *style) const
{
	gen_named_node(xml, "hbox", String<8>(id), [&] () {

		gen_named_node(xml, "float", "left", [&] () {
			xml.attribute("west", "yes");

			xml.node("hbox", [&] () {
				gen_named_node(xml, "button", "button", [&] () {

					if (selected)
						xml.attribute("selected", "yes");

					xml.attribute("style", style);
					xml.node("hbox", [&] () { });
				});
				gen_named_node(xml, "label", "name", [&] () {
					xml.attribute("text", text); });
			});
		});

		gen_named_node(xml, "hbox", "right", [&] () { });
	});
}

void Cpu::View::_gen_back_entry(Xml_generator &xml,
                                Session_label const &label) const
{
	gen_named_node(xml, "hbox", "back", [&] () {
		gen_named_node(xml, "float", "left", [&] () {
			xml.attribute("west", "yes");
			xml.node("hbox", [&] () {
				xml.node("button", [&] () {
					xml.attribute("selected", "yes");
					xml.attribute("style", "back");
					xml.node("hbox", [&] () { });
				});
				xml.node("label", [&] () {
					xml.attribute("font", "title/regular");
					xml.attribute("text", label);
				});
			});
		});
		gen_named_node(xml, "hbox", "right", [&] () { });
	});
}

void Cpu::View::gen_config(Reporter::Xml_generator &xml, Xml_node const &input)
{
	unsigned cmp_cnt = 1;

	input.for_each_sub_node("component", [&](Xml_node const &node) {
		unsigned const cmp_id = cmp_cnt++;
		if (cmp_id != selected_component)
			return;

		Session_label const label = node.attribute_value("label",
		                                                 Session_label::String());
		unsigned thread_cnt = 1;

		xml.node("component", [&] () {
			xml.attribute("default_policy", policy_to_string(_default_policy));
			xml.attribute("label", label);

			node.for_each_sub_node("thread", [&](Xml_node const &node) {
				unsigned const thread_id = thread_cnt++;
				if (thread_id != selected_thread)
					return;

				Thread::Name const thread = node.attribute_value("name", Thread::Name());

				xml.node("thread", [&] () {
					xml.attribute("name", thread);

					/* XXX - use enum */
					xml.attribute("policy", policy_to_string((POLICY)_policy.selected));

					if (_policy.selected == POLICY_PIN) {
						xml.attribute("xpos", _policy.pin.xpos());
						xml.attribute("ypos", _policy.pin.ypos());
					}
				});
			});
		});
	});
}

void Cpu::View::gen_dialog(Reporter::Xml_generator &xml, Xml_node const &input)
{
	unsigned cmp_cnt = 1;

	input.for_each_sub_node("component", [&](Xml_node const &node) {

		Session_label const label = node.attribute_value("label",
		                                                 Session_label::String());

		unsigned const xpos = node.attribute_value("xpos", 0U);
		unsigned const ypos = node.attribute_value("ypos", 0U);
		unsigned const width = node.attribute_value("width", 1U);
		unsigned const height = node.attribute_value("height", 1U);

		Affinity::Location const loc { int(xpos), int(ypos), width, height };

		unsigned const cmp_id = cmp_cnt++;

		if (!selected_component) {
			_gen_menu_entry(xml, cmp_id, label, false);
			return;
		}

		if (cmp_id != selected_component)
			return;

		if (_default_policy == POLICY_UNKNOWN && node.has_attribute("default_policy")) {
			String<32> const policy = node.attribute_value("default_policy", String<32>());
			_default_policy = string_to_policy(policy);
		}

		if (!selected_thread) {
			xml.node("hbox", [&] () {
				_gen_back_entry(xml, label);

				gen_named_node(xml, "button", "default", [&] () {
					xml.node("label", [&] () {
						xml.attribute("text", policy_to_string(_default_policy));
					});
				});

				gen_named_node(xml, "button", "iconize", [&] () {
					xml.node("label", [&] () {
						xml.attribute("text", affinity_iconize ? "text" : "icon");
					});
				});
			});
		}

		xml.node("vbox", [&] () {
			xml.attribute("name", Genode::String<16>("vbox ", cmp_id));

			unsigned thread_cnt = 1;

			node.for_each_sub_node("thread", [&](Xml_node const &node) {

				unsigned const thread_id = thread_cnt++;

				Thread::Name const name = node.attribute_value("name", Thread::Name());

				xml.node("hbox", [&] () {
					xml.attribute("name", thread_id);

					xml.node("float", [&] () {

						bool const back = selected_thread && selected_thread == thread_id;

						if (back)
							xml.attribute("west", "yes");
						else
							xml.attribute("east", "yes");

						if (!back && selected_thread)
							return;

						xml.node("hbox", [&] () {

							if (back)
								_gen_back_entry(xml, "");

							String<32> const schedule = node.attribute_value("policy", String<32>());
							enum POLICY const policy = string_to_policy(schedule);

#if 0
				gen_named_node(xml, "button", "radio", [&] () {
					xml.attribute("selected", "yes");
					xml.attribute("style", "radio");
					xml.node("hbox", [&] () { });
				});
#endif

							unsigned const local_xpos = node.attribute_value("xpos", 0U);
							unsigned const local_ypos = node.attribute_value("ypos", 0U);

							Affinity::Location hover { int(local_xpos), int(local_ypos), 1, 1 };

							gen_named_node(xml, "label", "thread", [&] () {
								xml.attribute("text", name);
							});

							if (thread_id != selected_thread) {
								gen_named_node(xml, "label", "policy", [&] () {
									xml.attribute("text",
									              String<32>(", policy: ",
									                         policy_to_short(policy),
									                         ", CPUs:"));
								});
							}

							if (back || affinity_iconize) {
								Cpu::Affinity_dialog dialog { _space, loc, hover };
								dialog._gen_affinity_entry(xml);
							} else {
								xml.node("label", [&] () {
									String<32> text(" ", hover.xpos(), "x", hover.ypos(),
								                    " -> ", loc.xpos(), "x", loc.ypos(),
								                    " + ", loc.width(), "x", loc.height(),
		                                            " (", _space.width(), "x", _space.height(), ")");
									xml.attribute("text", text);
								});
							}

							if (thread_id != selected_thread) {
								xml.node("button", [&] () {
									xml.attribute("style", "enter");
									xml.node("hbox", [&] () { });
								});
							}
						});
					});
				});

				if (selected_thread && thread_id == selected_thread) {

					bool const fixed = node.attribute_value("enforced", false);

					/* XXX use enum everywhere */
					enum POLICY policy = (POLICY)_policy.selected;
					if (policy == POLICY_UNKNOWN) {

						String<32> const schedule = node.attribute_value("policy", String<32>());

						policy = string_to_policy(schedule);

						_policy.selected = policy;
						_policy.previous = _policy.selected;
					}

					gen_named_node(xml, "label", "text", [&] {
						if (fixed)
							xml.attribute("text", String<32>("Migration policy is fix: "));
						else
							xml.attribute("text", String<32>("Select migration policy: "));
					});
					gen_named_node(xml, "hbox", "select", [&] {
						xml.node("vbox", [&] () {
							if (policy == POLICY_UNKNOWN)
								_gen_menu_entry(xml, POLICY_UNKNOWN, "Unknown", true);

							if (!fixed || policy == POLICY_NONE)
								_gen_menu_entry(xml, POLICY_NONE, "None", policy == POLICY_NONE);
							if (!fixed || policy == POLICY_PIN)
								_gen_menu_entry(xml, POLICY_PIN, "Pin", policy == POLICY_PIN);

							if ((_policy.selected == POLICY_PIN) &&
							    (_policy.previous != _policy.selected))
							{
								_policy.pin_loc = loc;
								Cpu::Affinity_dialog dialog { _space, loc, _policy.pin };
								dialog._gen_affinity_entry(xml);
							}

							if (!fixed || policy == POLICY_ROUND_ROBIN)
								_gen_menu_entry(xml, POLICY_ROUND_ROBIN, "Round-Robin", policy == POLICY_ROUND_ROBIN);
							if (!fixed || policy == POLICY_MAX_UTILIZE)
								_gen_menu_entry(xml, POLICY_MAX_UTILIZE, "Max utilize", policy == POLICY_MAX_UTILIZE);
						});
					});

					if (_policy.previous != _policy.selected) {
						gen_named_node(xml, "hbox", "confirm", [&] {
							xml.node("button", [&] () {
								xml.node("label", [&] () {
									xml.attribute("text", "Confirm");
								});
							});
						});
					}
				}
			});
		});
	});
}

void Component::construct(Genode::Env &env)
{
	static Cpu::View view(env);
}
