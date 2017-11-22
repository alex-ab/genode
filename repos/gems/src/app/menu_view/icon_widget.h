/*
 * \brief  Widget that simply shows an icon
 * \author Josef Soentgen
 * \date   2017-11-22
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _ICON_WIDGET_H_
#define _ICON_WIDGET_H_

/* demo includes */
#include <scout_gfx/icon_painter.h>

/* local includes */
#include "widget.h"

namespace Menu_view { struct Icon_widget; }


struct Menu_view::Icon_widget : Widget
{
	Texture<Pixel_rgb888> const * default_texture { nullptr };

	unsigned int alpha { 255 };

	Icon_widget(Widget_factory &factory, Xml_node node, Unique_id unique_id)
	: Widget(factory, node, unique_id) { }

	void update(Xml_node node)
	{
		typedef String<32> File;

		File file = node.attribute_value("file", File());
		if (file == "") {
			default_texture = nullptr;
			return;
		}

		default_texture = _factory.styles.texture(node, file.string());

		alpha = node.attribute_value("alpha", 255u);
		if (alpha > 255) { alpha = 255; }
	}

	Area min_size() const override
	{
		if (!default_texture) { return Area(0, 0); }

		Area const texture_size = default_texture->size();
		return Area(texture_size.w(), texture_size.h());
	}

	void draw(Surface<Pixel_rgb888> &pixel_surface,
	          Surface<Pixel_alpha8> &alpha_surface,
	          Point at) const
	{
		if (!default_texture) { return; }

		Area const texture_size = default_texture->size();
		Rect const texture_rect(at, texture_size);

		Icon_painter::paint(pixel_surface, texture_rect, *default_texture, alpha);
		Icon_painter::paint(alpha_surface, texture_rect, *default_texture, alpha);
	}
};

#endif /* _ICON_WIDGET_H_ */
