/*
 * \brief  Deploy option
 * \author Norman Feske
 * \date   2026-03-05
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _OPTION_H_
#define _OPTION_H_

/* Genode includes */
#include <util/list_model.h>
#include <base/attached_rom_dataspace.h>

/* local includes */
#include "alias.h"

namespace Depot_deploy { class Option; }


struct Depot_deploy::Option : List_model<Option>::Element
{
	List_model<Child> children { };
	List_model<Alias> aliases  { };

	using Name = Option_name;

	struct Attr
	{
		Name name;
		bool required;

		static Attr from_node(Node const &node)
		{
			return { .name     = node.attribute_value("name",     Name()),
			         .required = node.attribute_value("required", false) };
		}

		bool operator == (Attr const &other) const
		{
			return other.name == name && other.required == required;
		}
	};

	Attr const attr;

	bool _applied = false;

	struct Action : Interface
	{
		virtual void deploy_option_changed(Name const &, Node const &) = 0;
	};

	struct Watched_rom
	{
		Action &_action;

		Name const &_name;

		Attached_rom_dataspace _ds;

		Signal_handler<Watched_rom> _handler;

		bool defined = false;

		void _handle()
		{
			_ds.update();
			defined = _ds.node().type() == "option";
			_action.deploy_option_changed(_name, _ds.node());
		}

		Watched_rom(Env &env, Name const &name, Action &action)
		:
			_action(action), _name(name),
			_ds(env, Name("option -> ", name).string()),
			_handler(env.ep(), *this, &Watched_rom::_handle)
		{
			_ds.sigh(_handler);
			_handler.local_submit();
		}
	};

	Constructible<Watched_rom> _watched_rom { };

	Option(Attr const &attr) : attr(attr) { };

	void watch(Env &env, Action &action)
	{
		if (!_watched_rom.constructed())
			_watched_rom.construct(env, attr.name, action);
	}

	bool defined() const
	{
		return _watched_rom.constructed() && _watched_rom->defined;
	}

	/*
	 * \return true if config had any effect
	 */
	Progress apply(Child_dict &child_dict, Alias_dict &alias_dict,
	               Allocator &alloc, Node const &option)
	{
		Progress result = STALLED;

		/* detect initial application of an option without any content as progress */
		bool const orig_applied = _applied;
		_applied = type_matches(option);
		if (orig_applied != _applied)
			result = PROGRESSED;

		if (Child::update_from_node(children, child_dict, alloc, option).progressed)
			result = PROGRESSED;

		if (Alias::update_from_node(aliases, alias_dict, alloc, option).progressed)
			result = PROGRESSED;

		return result;
	}

	/**
	 * List_model::Element
	 */
	bool matches(Node const &node) const { return attr == Attr::from_node(node); }

	/**
	 * List_model::Element
	 */
	static bool type_matches(Node const &node) { return node.has_type("option"); }
};

#endif /* _OPTION_H_ */
