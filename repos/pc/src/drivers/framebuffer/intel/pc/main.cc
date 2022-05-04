/*
 * \brief  Linux Intel framebuffer driver port
 * \author Alexander Boettcher
 * \date   2022-03-08
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <timer_session/connection.h>
#include <capture_session/connection.h>
#include <os/pixel_rgb888.h>
#include <os/reporter.h>
#include <util/reconstructible.h>

/* emulation includes */
#include <lx_emul/init.h>
#include <lx_emul/fb.h>
#include <lx_emul/task.h>
#include <lx_kit/env.h>
#include <lx_kit/init.h>

/* local includes */
extern "C" {
#include "lx_i915.h"
}


extern struct task_struct * lx_user_task;


namespace Framebuffer {
	using namespace Genode;
	struct Driver;
}


struct Framebuffer::Driver
{
	Env                    &env;
	Timer::Connection       timer    { env };
	Heap                    heap     { env.ram(), env.rm() };
	Attached_rom_dataspace  config   { env, "config" };
	Reporter                reporter { env, "connectors" };

	Signal_handler<Driver>  config_handler { env.ep(), *this,
	                                        &Driver::config_update };
	Signal_handler<Driver>  timer_handler  { env.ep(), *this,
	                                        &Driver::handle_timer };

	struct Connector {
		using Space = Id_space<Connector>;
		using Id    = Space::Id;

		Space::Element id_element;

		Connector(Space &space, Id id) : id_element(*this, space, id) { }

		addr_t        base      { };
		Capture::Area size      { };
		Capture::Area size_phys { };
		unsigned      offset_x  { };
	};

	Connector::Space ids { };

	class Fb
	{
		private:

			Capture::Area                              _size_all { };

			Constructible<Capture::Connection>         _capture { };
			Constructible<Capture::Connection::Screen> _captured_screen { };

			/*
			 * Non_copyable
			 */
			Fb(const Fb&);
			Fb & operator=(const Fb&);

			unsigned max_height(Connector::Space const &ids) const
			{
				unsigned height = 0;

				ids.for_each<Connector>([&](auto &connector) {
					if (connector.size.h() > height)
						height = connector.size.h();
				});

				return height;
			}

			struct Blit
			{
				typedef Genode::Surface_base::Point Point;
				typedef Genode::Surface_base::Rect  Rect;


				template <typename PT>
				static inline void paint(Genode::Surface<PT>       &surface,
				                         Genode::Texture<PT> const &texture,
				                         Point               const  offset_in_texture)
				{
					Rect const texture_clip = Rect(offset_in_texture + surface.clip().p1(),
					                               surface.clip().area());
					Rect const surface_clip = surface.clip();

					int const src_w = texture.size().w();
					int const dst_w = surface.size().w();

					/* calculate offset of first texture pixel to copy */
					unsigned long const tex_start_offset = texture_clip.y1() * src_w
					                                     + texture_clip.x1();

					/* start address of source pixels */
					PT const * const src = texture.pixel() + tex_start_offset;

					/* start address of destination pixels */
					PT * const dst = surface.addr() + surface_clip.y1() * dst_w
					               + surface_clip.x1();

					blit(src, (unsigned)(src_w*sizeof(PT)),
					     dst, (unsigned)(dst_w*sizeof(PT)),
					     (unsigned)(surface_clip.w()*sizeof(PT)), surface_clip.h());

					surface.flush_pixels(surface_clip);
				}
			};

		public:

			void paint(Connector::Space &ids)
			{
				using Pixel = Capture::Pixel;
				using Affected_rects = Capture::Session::Affected_rects;

				_captured_screen->with_texture([&] (Texture<Pixel> const &texture) {

					Affected_rects const affected = _capture->capture_at(Capture::Point(0, 0));

					affected.for_each_rect([&] (Capture::Rect const rect) {

						ids.for_each<Connector>([&](auto &connector) {

							Capture::Point point(connector.offset_x, 0);

//							log("paint ", connector.id_element.id().value);

							using Pixel = Capture::Pixel;
							Surface<Pixel> surface((Pixel*)connector.base,
							                       connector.size_phys);

							if (rect.x2() >= point.x()) {
								surface.clip(Capture::Rect(Point(0, 0),
								                           Area(unsigned(rect.x2() - point.x() + 1),
								                                unsigned(rect.y2() - point.y() + 1))));
								Blit::paint(surface, texture, point);
							}

//							point = point + Capture::Point(connector.size.w(), 0);
						});
					});
				});
			}

			Fb() { }

			bool constructed() { return _captured_screen.constructed(); }

			Capture::Area size() const { return _size_all; }

			void reconstruct(Env & env)
			{
				_captured_screen.destruct();
				_capture.destruct();

				if (_size_all.valid()) {
					_capture.construct(env);
					_captured_screen.construct(*_capture, env.rm(), _size_all);
				}
			}

			bool setup(Connector &id, Connector::Space const &ids,
			           addr_t const base, unsigned const offset_x,
			           Capture::Area &size, Capture::Area &size_phys)
			{
				bool same = (base      == id.base) &&
				            (size      == id.size) &&
				            (size_phys == id.size_phys) &&
				            (offset_x  == id.offset_x);

				if (!same) {
					/* reduce size by previous values */
					_size_all = Area(_size_all.w() - id.size.w(),
					                 _size_all.h());

					id.base      = base;
					id.size      = size;
					id.size_phys = size_phys;
					id.offset_x  = offset_x;

					/* adjust size to new values */
					_size_all = Area(_size_all.w() + id.size.w(),
					                 max_height(ids));
				}

				return same;
			}
	};

	Fb fb {};

	void config_update();
	void generate_report(void *);
	void lookup_config(char const *, struct genode_mode &mode);

	void handle_timer()
	{
		if (fb.constructed()) { fb.paint(ids); }
	}

	Driver(Env &env) : env(env)
	{
		Lx_kit::initialize(env);
		env.exec_static_constructors();

		config.sigh(config_handler);
	}

	void start()
	{
		log("--- Intel framebuffer driver started ---");

		lx_emul_start_kernel(nullptr);

		timer.sigh(timer_handler);
		timer.trigger_periodic(20*1000);
	}
};


enum { MAX_BRIGHTNESS = 100u };


void Framebuffer::Driver::config_update()
{
	config.update();

	if (!config.valid() || !lx_user_task)
		return;

	lx_emul_task_unblock(lx_user_task);
	Lx_kit::env().scheduler.schedule();
}


static Framebuffer::Driver & driver(Genode::Env & env)
{
	static Framebuffer::Driver driver(env);
	return driver;
}


void Framebuffer::Driver::generate_report(void *lx_data)
{
	/* check for report configuration option */
	try {
		reporter.enabled(config.xml().sub_node("report")
		                 .attribute_value(reporter.name().string(), false));
	} catch (...) {
		Genode::warning("Failed to enable report");
		reporter.enabled(false);
	}

	if (!reporter.enabled()) return;

	try {
		Genode::Reporter::Xml_generator xml(reporter, [&] ()
		{
			lx_emul_i915_report(lx_data, &xml);
		});
	} catch (...) {
		Genode::warning("Failed to generate report");
	}
}


void Framebuffer::Driver::lookup_config(char const * const name,
                                        struct genode_mode &mode)
{
	if (!config.valid())
		return;

	unsigned force_width  = config.xml().attribute_value("force_width",  0u);
	unsigned force_height = config.xml().attribute_value("force_height", 0u);

	/* iterate independently of force* ever to get brightness and hz */
	config.xml().for_each_sub_node("connector", [&] (Xml_node &node) {
		typedef String<32> Name;
		Name const con_policy = node.attribute_value("name", Name());
		if (con_policy != name)
			return;

		mode.enabled = node.attribute_value("enabled", true);
		if (!mode.enabled)
			return;

		mode.brightness = node.attribute_value("brightness",
		                                       unsigned(MAX_BRIGHTNESS + 1));

		mode.width          = node.attribute_value("width",  0U);
		mode.height         = node.attribute_value("height", 0U);
		mode.hz             = node.attribute_value("hz", 0U);
		mode.id             = node.attribute_value("mode_id", 0U);
		mode.mirror         = node.attribute_value("mirror", true);
		mode.screen_offsetx = node.attribute_value("screen_offsetx", 0U);
	});

	/* enforce forced width/height if configured */
	mode.preferred = force_width && force_height;
	if (mode.preferred) {
		mode.width          = force_width;
		mode.height         = force_height;
		mode.id             = 0;
		mode.mirror         = true;
		mode.screen_offsetx = 0;
	}
}


/**
 * Can be called already as side-effect of `lx_emul_start_kernel`,
 * that's why the Driver object needs to be constructed already here.
 */
extern "C" void lx_emul_framebuffer_ready(unsigned connector_id,
                                          void * base, unsigned long,
                                          unsigned xres, unsigned yres,
                                          unsigned phys_width,
                                          unsigned phys_height,
                                          unsigned screen_offsetx,
                                          unsigned screen_offsety)
{
	auto &env = Lx_kit::env().env;
	auto &drv = driver(env);
	auto &fb  = drv.fb;
	bool allocated = false;

	typedef Framebuffer::Driver::Connector Id;

	auto const id = Id::Id { connector_id };

	do {
		try {
			drv.ids.apply<Id>(id, [&](Id &id) {

				Capture::Area area(xres, yres);
				Capture::Area area_phys(phys_width, phys_height);

				bool const same = fb.setup(id, drv.ids, (Genode::addr_t)base,
				                           screen_offsetx, area, area_phys);

				if (same)
					return;

				/* clear artefacts */
				if (base && (area != area_phys))
					Genode::memset(base, 0, area_phys.count() * 4);

				log("framebuffer ", fb.size(),
				    " - connector id=", id.id_element.id().value,
				    ", virtual=", xres, "x", yres,
				    " offset=", screen_offsetx, "x", screen_offsety,
				    ", physical=", phys_width, "x", phys_height);

				fb.reconstruct(env);
			});
			break;
		} catch (Id::Space::Unknown_id) {
			/* ignore unused connector - don't need a object for it */
			if (!base)
				break;

			if (!allocated) {
				new (drv.heap) Id (drv.ids, id);
				allocated = true;
			}
		}
	} while (true);
}


extern "C" void lx_emul_i915_hotplug_connector(void *data)
{
	Genode::Env &env = Lx_kit::env().env;
	driver(env).generate_report(data);
}


void lx_emul_i915_report_connector(void * lx_data, void * genode_xml,
                                   char const *name, char const connected,
                                   unsigned brightness)
{
	auto &xml = *reinterpret_cast<Genode::Reporter::Xml_generator *>(genode_xml);

	xml.node("connector", [&] ()
	{
		xml.attribute("name", name);
		xml.attribute("connected", !!connected);

		/* insane values means no brightness support - we use percentage */
		if (brightness <= MAX_BRIGHTNESS)
			xml.attribute("brightness", brightness);

		lx_emul_i915_iterate_modes(lx_data, &xml);
	});

	/* re-read config on connector change */
	Genode::Signal_transmitter(driver(Lx_kit::env().env).config_handler).submit();
}


void lx_emul_i915_report_modes(void * genode_xml, struct genode_mode *mode)
{
	if (!genode_xml || !mode)
		return;

	auto &xml = *reinterpret_cast<Genode::Reporter::Xml_generator *>(genode_xml);

	xml.node("mode", [&] ()
	{
		xml.attribute("width",     mode->width);
		xml.attribute("height",    mode->height);
		xml.attribute("hz",        mode->hz);
		xml.attribute("mode_id",   mode->id);
		xml.attribute("mode_name", mode->name);
		if (mode->preferred)
			xml.attribute("preferred", true);
	});
}


void lx_emul_i915_connector_config(char * name, struct genode_mode * mode)
{
	if (!mode || !name)
		return;

	Genode::Env &env = Lx_kit::env().env;
	driver(env).lookup_config(name, *mode);
}


void Component::construct(Genode::Env &env)
{
	driver(env).start();
}
