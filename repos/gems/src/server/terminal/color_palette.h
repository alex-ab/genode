/*
 * \brief  Terminal color palette
 * \author Norman Feske
 * \author Christian Helmuth
 * \date   2018-02-06
 */

/*
 * Copyright (C) 2018-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _COLOR_PALETTE_H_
#define _COLOR_PALETTE_H_

/* Genode includes */
#include <util/color.h>

/* local includes */
#include "types.h"

namespace Terminal { class Color_palette; }


class Terminal::Color_palette
{
	public:

		struct Index { unsigned value; };

		struct Highlighted { bool value; };

	private:

		static constexpr unsigned NUM_ENTRIES = 16U;

		static Color _dimmed(Color c)
		{
			return Color::clamped_rgb(c.r*3/4, c.g*3/4, c.b*3/4);
		}

		struct Entry
		{
			Color fg, bg;

			static Entry from_cstring(char const *s)
			{
				Color c { };
				c.parse(Span::from_cstring(s));
				return { .fg = c, .bg = _dimmed(c) };
			}

			static Entry from_node(Node const &node)
			{
				Color const fg = node.attribute_value("fg", Color { });
				Color const bg = node.attribute_value("bg", _dimmed(fg));

				return { .fg = fg, .bg = bg };
			}

			static Entry from_deprecated_node(Node const &node)
			{
				Color const fg = node.attribute_value("value", Color { });
				return { .fg = fg, .bg = _dimmed(fg) };
			}
		};

		Entry _entries[NUM_ENTRIES] { };

		void _apply_default()
		{
			static constexpr char const * _default_palette[NUM_ENTRIES] {
				"#000000", /*  0  black          */
				"#AC4142", /*  1  red            */
				"#90A959", /*  2  green          */
				"#F4BF75", /*  3  yellow         */
				"#7686BD", /*  4  blue           */
				"#AA759F", /*  5  magenta        */
				"#75B5AA", /*  6  cyan           */
				"#D0D0D0", /*  7  white          */
				"#101010", /*  8  bright black   */
				"#AC4142", /*  9  bright red     */
				"#90A959", /* 10  bright green   */
				"#F4BF75", /* 11  bright yellow  */
				"#6A9FB5", /* 12  bright blue    */
				"#AA759F", /* 13  bright magenta */
				"#75B5AA", /* 14  bright cyan    */
				"#F5F5F5", /* 15  bright white   */
			};

			for (unsigned i = 0; i < NUM_ENTRIES; i++)
				_entries[i] = Entry::from_cstring(_default_palette[i]);
		}

		void _apply_palette(Node const &palette)
		{
			bool warn_deprecated = false;
			palette.for_each_sub_node("color", [&] (Node const &node) {

				/* compatibility to deprecated config format */
				if (!node.has_attribute("name")) {
					warn_deprecated = true;

					if (!node.has_attribute("index")) return;
					if (!node.has_attribute("value")) return;

					unsigned const i = node.attribute_value("index", 0U);
					if (i < NUM_ENTRIES) _entries[i] = Entry::from_deprecated_node(node);
					return;
				}

				unsigned const i = node.attribute_value("name", 0U);
				if (i < NUM_ENTRIES) _entries[i] = Entry::from_node(node);
			});
			if (warn_deprecated) {
				warning("colors configured via deprecated tags 'index' and 'value',");
				warning("use '+ color <index> | fg: <value>' instead");
			}
		}

		Color _with_entry(Index index, Highlighted highlight, auto const &fn) const
		{
			unsigned const i = index.value + highlight.value*NUM_ENTRIES/2;

			return i < NUM_ENTRIES ? fn(_entries[i]) : Color { };
		}

	public:

		Color_palette() { _apply_default(); }

		void apply_config(Node const &config)
		{
			_apply_default();
			config.for_each_sub_node("palette", [&] (Node const &palette) {
				_apply_palette(palette); });
		}

		Color foreground(Index i, Highlighted h) const
		{
			return _with_entry(i, h, [&] (Entry e) { return e.fg; });
		}

		Color background(Index i, Highlighted h) const
		{
			return _with_entry(i, h, [&] (Entry e) { return e.bg; });
		}
};

#endif /* _COLOR_PALETTE_H_ */
