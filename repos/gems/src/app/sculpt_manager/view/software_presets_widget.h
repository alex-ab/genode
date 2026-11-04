/*
 * \brief  Widget for the deploy presets
 * \author Norman Feske
 * \date   2023-01-11
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _VIEW__SOFTWARE_PRESETS_WIDGET_H_
#define _VIEW__SOFTWARE_PRESETS_WIDGET_H_

#include <model/presets.h>
#include <view/dialog.h>
#include <string.h>

namespace Sculpt { struct Software_presets_widget; }


struct Sculpt::Software_presets_widget : Widget<Frame>
{
	struct Index : Id { } _selected { };

	struct Preset : Widget<Vbox>
	{
		using Radio = Hosted<Vbox, Radio_select_button<Index>>;
		using Load  = Hosted<Vbox, Float, Deferred_action_button>;

		void view(Scope<Vbox> &s, Presets::Info const &preset, Load const &load,
		          Index const &index, Index const &selected) const
		{
			s.widget(Radio { Id { "radio" }, index },
			         selected, Presets::Info::Name { " ", Pretty(preset.name) });

			if (index.value != selected.value)
				return;

			s.sub_scope<Vgap>();

			s.sub_scope<Float>([&] (Scope<Vbox, Float> &s) {
				s.sub_scope<Label>(preset.text); });

			s.sub_scope<Vgap>();

			s.sub_scope<Float>([&] (Scope<Vbox, Float> &s) {
				s.widget(load, [&] (Scope<Button> &s) {
					s.sub_scope<Label>(" Load "); }); });

			s.sub_scope<Vgap>();
		}

		void click(Clicked_at const &at, Load &load) { load.propagate(at); }

		void clack(Clacked_at const &at, Load &load, auto const &fn)
		{
			load.propagate(at, fn);
		}
	};

	/* use one button to preserve 'Deferred_action_button' state across presets */
	Preset::Load _load { Id { "load" } };

	using Hosted_preset = Hosted<Frame, Vbox, Preset>;

	void view(Scope<Frame> &s, Presets const &presets) const
	{
		s.sub_scope<Vbox>([&] (Scope<Frame, Vbox> &s) {
			s.sub_scope<Min_ex>(35);
			unsigned i = 0;
			presets.for_each([&] (Presets::Info const &info) {
				Index const index { ++i };
				Hosted_preset const hosted { index };
				s.widget(hosted, info, _load, index, _selected); }); });
	}

	void _with_selected_preset(Presets const &presets, auto const &fn)
	{
		unsigned i = 0;
		presets.for_each([&] (Presets::Info const &info) {
			if (Id::Value(++i) == _selected.value)
				fn(Index { i }, info); });
	}

	void click(Clicked_at const &at, Presets const &presets)
	{
		/* unfold preset info */
		Id const id = at.matching_id<Frame, Vbox, Vbox>();
		if (id.valid())
			_selected.value = id.value;

		_with_selected_preset(presets, [&] (Index const i, Presets::Info const &) {
			Hosted_preset { i }.propagate(at, _load); });
	}

	struct Action : Interface
	{
		virtual void load_deploy_preset(Presets::Info::Name const &) = 0;
	};

	void clack(Clacked_at const &at, Presets const &presets, Action &action)
	{
		_with_selected_preset(presets, [&] (Index const i, Presets::Info const &info) {
			Hosted_preset { i }.propagate(at, _load, [&] {
				action.load_deploy_preset(info.name);
				_selected = { };
			});
		});
	}
};

#endif /* _VIEW__SOFTWARE_PRESETS_WIDGET_H_ */
