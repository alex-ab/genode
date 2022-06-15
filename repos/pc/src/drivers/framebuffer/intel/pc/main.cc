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

		Constructible<Capture::Connection>         capture { };
		Constructible<Capture::Connection::Screen> captured_screen { };
	};

	Connector::Space ids { };

	class Fb
	{
		private:

			/*
			 * Non_copyable
			 */
			Fb(const Fb&);
			Fb & operator=(const Fb&);

		public:

			void paint(Connector::Space &ids)
			{
				ids.for_each<Connector>([&](auto &connector) {
					if (!connector.capture.constructed() ||
					    !connector.captured_screen.constructed())
						return;

					using Pixel = Capture::Pixel;

					connector.captured_screen->with_texture([&] (Texture<Pixel> const &texture) {

						auto const affected = connector.capture->capture_at(Capture::Point(connector.offset_x, 0));
						affected.for_each_rect([&] (Capture::Rect const rect) {

							Surface<Pixel> surface((Pixel*)connector.base,
							                       connector.size_phys);
							surface.clip(rect);
							Blit_painter::paint(surface, texture, Capture::Point(0, 0));
						});
					});
				});
			}

			Fb() { }

			bool setup(Connector &id, Env &env,
			           addr_t const base, unsigned const offset_x,
			           Capture::Area &size, Capture::Area &size_phys)
			{
				bool same = (base      == id.base) &&
				            (size      == id.size) &&
				            (size_phys == id.size_phys) &&
				            (offset_x  == id.offset_x);

				if (!same) {
					id.base      = base;
					id.size      = size;
					id.size_phys = size_phys;
					id.offset_x  = offset_x;

					if (id.size.valid()) {
						id.capture.construct(env);
						id.captured_screen.construct(*id.capture, env.rm(), id.size);
					} else {
						id.captured_screen.destruct();
						id.capture.destruct();
					}
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
		fb.paint(ids);
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

	void report_updated()
	{
		bool apply_config = true;

		if (config.valid())
			apply_config = config.xml().attribute_value("apply_on_hotplug", apply_config);

		/* trigger re-read config on connector change */
		if (apply_config)
			Genode::Signal_transmitter(config_handler).submit();
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

				Capture::Area area(xres + screen_offsetx, yres);
				Capture::Area area_phys(phys_width, phys_height);

				bool const same = fb.setup(id, env, (Genode::addr_t)base,
				                           screen_offsetx, area, area_phys);

				if (same)
					return;

				/* clear artefacts */
				if (base && (area != area_phys))
					Genode::memset(base, 0, area_phys.count() * 4);

				Genode::log("framebuffer ",
				    " - connector id=", id.id_element.id().value,
				    ", virtual=", xres, "x", yres,
				    " offset=", screen_offsetx, "x", screen_offsety,
				    ", physical=", phys_width, "x", phys_height);
			});
			break;
		} catch (Id::Space::Unknown_id) {
			/* ignore unused connector - don't need a object for it */
			if (!base || allocated)
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

	driver(Lx_kit::env().env).report_updated();
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
