/*
 * \brief  Widget that displays a single line of plain text
 * \author Norman Feske
 * \date   2009-09-11
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LABEL_WIDGET_H_
#define _LABEL_WIDGET_H_

/* local includes */
#include <widget_factory.h>

namespace Menu_view { struct Label_widget; }

struct Menu_view::Label_widget : Widget
{
	Text_painter::Font const *font = nullptr;

	enum { LABEL_MAX_LEN = 256 };

	typedef String<200> Text;
	Text text { };

	Color text_color { 0, 0, 0 };

	Color _update_color(Xml_node node)
	{
		try {
			Color color;
			node.attribute("color").value(&color);
			return color;
		} catch (Xml_node::Nonexistent_attribute) {
			return text_color;
		}
	}

	enum Alignment { ALIGN_CENTER, ALIGN_LEFT, ALIGN_RIGHT, };

	Alignment _update_alignment(Xml_node node)
	{
		try {
			String<32> value;
			node.attribute("align").value(&value);
			if      (value == "left")  return ALIGN_LEFT;
			else if (value == "right") return ALIGN_RIGHT;
			else                       return ALIGN_CENTER;
		} catch (Xml_node::Nonexistent_attribute) { }

		return ALIGN_CENTER;
	}

	Alignment text_alignment { ALIGN_CENTER };

	Label_widget(Widget_factory &factory, Xml_node node, Unique_id unique_id)
	:
		Widget(factory, node, unique_id)
	{ }

	void update(Xml_node node) override
	{
		font = _factory.styles.font(node);
		text = node.attribute_value("text", Text(""));

		text_color = _update_color(node);
		text_alignment = _update_alignment(node);
	}

	Area min_size() const override
	{
		if (!font)
			return Area(0, 0);

		return Area(font->string_width(text.string()).decimal(),
		            font->height());
	}

	void draw(Surface<Pixel_rgb888> &pixel_surface,
	          Surface<Pixel_alpha8> &alpha_surface,
	          Point at) const override
	{
		if (!font) return;

		Area text_size = min_size();

		int const dx = (int)geometry().w() - text_size.w(),
		          dy = (int)geometry().h() - text_size.h();

		int const x = text_alignment == ALIGN_CENTER ? dx/2 :
		              text_alignment == ALIGN_LEFT   ?    0 : dx;

		Point const centered = at + Point(x, dy/2);

		Text_painter::paint(pixel_surface,
		                    Text_painter::Position(centered.x(), centered.y()),
		                    *font, text_color, text.string());

		Text_painter::paint(alpha_surface,
		                    Text_painter::Position(centered.x(), centered.y()),
		                    *font, Color(255, 255, 255), text.string());
	}

	private:

		/**
		 * Noncopyable
		 */
		Label_widget(Label_widget const &);
		Label_widget &operator = (Label_widget const &);
};

#endif /* _LABEL_WIDGET_H_ */
