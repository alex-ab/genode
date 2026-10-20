/*
 * \brief  Nitpicker pointer with support for VirtualBox-defined shapes
 * \author Norman Feske
 * \author Christian Helmuth
 * \author Christian Prochaska
 * \date   2014-07-02
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>
#include <base/heap.h>
#include <base/attached_rom_dataspace.h>
#include <os/pixel_alpha8.h>
#include <os/pixel_rgb888.h>
#include <os/pixel_rgb565.h>
#include <os/surface.h>
#include <os/texture_rgb888.h>
#include <gui_session/connection.h>
#include <report_rom/report_service.h>
#include <pointer/shape_report.h>

/* local includes */
#include "rom_registry.h"
#include "big_mouse.h"

namespace Pointer {
	using namespace Genode;
	class Main;

	struct Scale
	{
		unsigned x, y;

		static Scale from_node(Node const &node)
		{
			unsigned const scale = node.attribute_value("scale", 1u);
			return { .x = node.attribute_value("scale_x", scale),
			         .y = node.attribute_value("scale_y", scale) };
		}
	};

	/**
	 * Paint scaled texture to surface
	 *
	 * Surface size must be multiple ot texture size. No clipping.
	 */
	template <typename DST_PT, typename SRC_PT>
	static inline void paint_scaled(Surface<DST_PT>       &surface,
	                                Texture<SRC_PT> const &texture)
	{
		if (!texture.size().count() || !texture.pixel() || !texture.alpha())
			return;

		unsigned const x_factor = surface.size().w / texture.size().w,
		               y_factor = surface.size().h / texture.size().h;
		if (!x_factor || !y_factor)
			return;

		DST_PT *dst = surface.addr();

		for (unsigned y = 0, h = surface.size().h; y < h; y++) {

			size_t const src_line_offset = (y/y_factor)*texture.size().w;

			SRC_PT  const *src_pixel = texture.pixel() + src_line_offset;
			uint8_t const *src_alpha = texture.alpha() + src_line_offset;

			for (unsigned x = 0, w = surface.size().w; x < w; x++) {

				SRC_PT  const pixel = src_pixel[x/x_factor];
				uint8_t const alpha = src_alpha[x/x_factor];

				*dst++ = DST_PT(pixel.r(), pixel.g(), pixel.b(), alpha);
			}
		}
	}
}


struct Pointer::Main : Rom::Reader
{
	Env &_env;

	Attached_rom_dataspace _config_rom { _env, "config" };

	struct Config
	{
		bool  verbose;
		bool  shapes;
		Scale scale;

		static Config from_node(Node const &node)
		{
			return {
				.verbose = node.attribute_value("verbose", false),
				.shapes  = node.attribute_value("shapes",  false),
				.scale   = Scale::from_node(node),
			};
		}
	};

	Config _config { };

	Signal_handler<Main> _config_handler { _env.ep(), *this, &Main::_handle_config };

	void _handle_config();

	Gui::Connection _gui { _env };

	Gui::Top_level_view _view { _gui };

	Gui::Area _current_pointer_size { };

	Dataspace_capability _pointer_ds { };

	void _update_texture(Gui::Point, Gui::Area, auto const &fn);
	void _show_default_pointer();
	void _update_pointer();

	bool _xray = false;

	Constructible<Attached_rom_dataspace> _hover_ds { };
	Constructible<Attached_rom_dataspace> _xray_ds  { };

	Signal_handler<Main> _hover_handler { _env.ep(), *this, &Main::_handle_hover };
	Signal_handler<Main> _xray_handler  { _env.ep(), *this, &Main::_handle_xray };

	Sliced_heap _sliced_heap { _env.ram(), _env.rm() };

	Rom::Registry _rom_registry { _sliced_heap, _env.ram(), _env.rm(), *this };

	Report::Root _report_root { _env, _sliced_heap, _rom_registry, _config.verbose };

	Session_label _hovered_label { };

	Attached_ram_dataspace _texture_pixel_ds { _env.ram(), _env.rm(),
	                                           Pointer::MAX_WIDTH  *
	                                           Pointer::MAX_HEIGHT *
	                                           sizeof(Pixel_rgb888) };

	Attached_ram_dataspace _texture_alpha_ds { _env.ram(), _env.rm(),
	                                           Pointer::MAX_WIDTH  *
	                                           Pointer::MAX_HEIGHT };

	void _show_shape_pointer(Shape_report &shape_report);
	void _handle_hover();
	void _handle_xray();

	/**
	 * Reader interface
	 */
	void mark_as_outdated()    override { }
	void mark_as_invalidated() override { }
	void notify_client()       override { _update_pointer(); }

	Main(Env &);
};


void Pointer::Main::_update_texture(Gui::Point hot, Gui::Area size, auto const &fn)
{
	if (size.w > MAX_WIDTH || size.h > MAX_HEIGHT)
		return;

	Texture<Pixel_rgb888> texture(_texture_pixel_ds.local_addr<Pixel_rgb888>(),
	                              _texture_alpha_ds.local_addr<uint8_t>(),
	                              size);
	fn(texture);

	Scale const scale = _config.scale;

	Gui::Area  const scaled_size { .w = size.w*scale.x,     .h = size.h*scale.y };
	Gui::Point const scaled_hot  { .x = hot.x*int(scale.x), .y = hot.y*int(scale.y) };

	if (scaled_size != _current_pointer_size) {

		_gui.buffer({ .area = scaled_size, .alpha = true });

		_pointer_ds = _gui.framebuffer.dataspace();

		_current_pointer_size = scaled_size;
	}

	Attached_dataspace ds { _env.rm(), _pointer_ds };

	Pixel_rgb888 *pixel = ds.local_addr<Pixel_rgb888>();

	Pixel_alpha8 *alpha = reinterpret_cast<Pixel_alpha8 *>(pixel + scaled_size.count());

	Surface<Pixel_rgb888> pixel_surface(pixel, scaled_size);
	Surface<Pixel_alpha8> alpha_surface(alpha, scaled_size);

	paint_scaled(pixel_surface, texture);
	paint_scaled(alpha_surface, texture);

	_gui.framebuffer.refresh({ { 0, 0 }, scaled_size });

	_view.geometry({ { -scaled_hot.x, -scaled_hot.y }, scaled_size });
}


void Pointer::Main::_show_default_pointer()
{
	Gui::Area size { big_mouse.w, big_mouse.h };

	_update_texture({ }, size, [&] (Texture<Pixel_rgb888> &texture) {

		/* import builtin shape given in RGB565 format */
		for (unsigned int y = 0; y < size.h; y++) {
			auto const * line_rgb565 = (Pixel_rgb565*)big_mouse.pixels[y];
			uint8_t line[size.w*4] { };
			for (unsigned x = 0; x < size.w; x++) {
				Pixel_rgb565 const pixel = line_rgb565[x];
				line[x*4+0] = uint8_t(pixel.r());
				line[x*4+1] = uint8_t(pixel.g());
				line[x*4+2] = uint8_t(pixel.b());
				line[x*4+3] = uint8_t(pixel.r() ? 255 : 0);
			}
			texture.rgba(line, size.w, y);
		}
	});
}


void Pointer::Main::_show_shape_pointer(Shape_report &shape_report)
{
	Gui::Area  size { shape_report.width, shape_report.height };
	Gui::Point hot  { int(shape_report.x_hot), int(shape_report.y_hot) };

	_update_texture(hot, size, [&] (Texture<Pixel_rgb888> &texture) {

		/* import client-provided shape */
		for (unsigned int y = 0; y < size.h; y++) {
			unsigned char *shape = shape_report.shape;
			unsigned char *line  = &shape[y * size.w * 4];
			texture.rgba(line, size.w, y);
		}
	});
}


void Pointer::Main::_update_pointer()
{
	Shape_report shape_report { };

	try {
		Rom::Readable_module &rom = _rom_registry.lookup(*this, _hovered_label);
		try {
			rom.read_content(*this, (char*)&shape_report, sizeof(shape_report));
		}
		catch (...) { }
		_rom_registry.release(*this, rom);
	}
	catch (...) { }

	if (shape_report.valid())
		_show_shape_pointer(shape_report);
	else
		_show_default_pointer();
}


void Pointer::Main::_handle_hover()
{
	Session_label const orig_hovered_label = _hovered_label;

	/* read new hover information from nitpicker's hover report */
	_hovered_label = { };
	if (_hover_ds.constructed()) {
		_hover_ds->update();
		_hovered_label = _hover_ds->node().attribute_value("label", String<160>());
		_hovered_label = _hovered_label.prefix();
	}

	if (_config.verbose)
		log("hovered_label: ", _hovered_label);

	if (orig_hovered_label != _hovered_label)
		_update_pointer();
}


void Pointer::Main::_handle_xray()
{
	bool const orig_xray = _xray;

	_xray = false;
	if (_xray_ds.constructed()) {
		_xray_ds->update();
		_xray = _xray_ds->node().attribute_value("enabled", false);
	}

	if (orig_xray != _xray)
		_update_pointer();
}


Pointer::Main::Main(Env &env) : _env(env)
{
	/*
	 * Try to allocate the Nitpicker buffer for the maximum supported
	 * pointer size to let the user know right from the start if the
	 * RAM quota is too low.
	 */

	_gui.buffer({ .area  = { Pointer::MAX_WIDTH, Pointer::MAX_HEIGHT },
	              .alpha = true });

	_config_rom.sigh(_config_handler);
	_handle_config();
}


void Pointer::Main::_handle_config()
{
	Config const orig_config = _config;

	_config_rom.update();
	_config = Config::from_node(_config_rom.node());

	if (_config.shapes && !_hover_ds.constructed()) {
		try {
			_hover_ds.construct(_env, "hover");
			_hover_ds->sigh(_hover_handler);
			_handle_hover();
		} catch (Rom_connection::Rom_connection_failed) {
			warning("Could not open ROM session for \"hover\".",
			        " This ROM is used for custom pointer shape support.");
		}

		try {
			_xray_ds.construct(_env, "xray");
			_xray_ds->sigh(_xray_handler);
			_handle_xray();
		} catch (Rom_connection::Rom_connection_failed) {
			warning("Could not open ROM session for \"xray\".",
			        " This ROM is used for custom pointer shape support.");
		}
	}

	_view.front();

	_update_pointer();

	if (!orig_config.shapes && _config.shapes)
		_env.parent().announce(_env.ep().manage(_report_root));
}


void Component::construct(Genode::Env &env) { static Pointer::Main main(env); }
