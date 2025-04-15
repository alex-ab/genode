/*
 * \brief  Sculpt framebuffer-driver management
 * \author Norman Feske
 * \date   2024-03-15
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _DRIVER__FB_H_
#define _DRIVER__FB_H_

#include <i2c_session/i2c_session.h>

namespace Sculpt { struct Fb_driver; }


struct Sculpt::Fb_driver : private Noncopyable
{
	using Fb_name = String<16>;

	struct Action : Interface
	{
		virtual void fb_connectors_changed() = 0;
	};

	Env    &_env;
	Action &_action;

	Fb_name _launcher_fb { };

	Constructible<Child_state> _boot_fb   { },
	                           _soc_fb    { };

	Constructible<Rom_handler<Fb_driver>> _connectors { };

	void _handle_connectors(Xml_node const &) { _action.fb_connectors_changed(); }

	Fb_name _fb_name() const
	{
		return _boot_fb .constructed() ? "boot_fb"
		     : _soc_fb  .constructed() ? "fb"
		     : _launcher_fb;
	}

	Fb_driver(Env &env, Action &action) : _env(env), _action(action) { }

	void gen_start_nodes(Xml_generator &xml) const
	{
		auto gen_capture_route = [&] (Xml_generator &xml)
		{
			gen_service_node<Capture::Session>(xml, [&] {
				xml.node("parent", [] { }); });
		};

		auto start_node = [&] (auto const &driver, auto const &binary, auto const &fn)
		{
			if (driver.constructed())
				xml.node("start", [&] {
					driver->gen_start_node_content(xml);
					gen_named_node(xml, "binary", binary);
					fn(); });
		};

		start_node(_boot_fb, "boot_fb", [&] {
			xml.node("heartbeat", [&] { });
			xml.node("route", [&] {
				gen_parent_rom_route(xml, "config", "config -> fb");
				gen_parent_rom_route(xml, "boot_fb");
				gen_parent_rom_route(xml, "platform_info");
				gen_parent_route<Io_mem_session>(xml);
				gen_capture_route(xml);
				gen_common_routes(xml);
			});
		});

		start_node(_soc_fb, "fb", [&] {
			xml.node("route", [&] {
				gen_parent_route<Platform::Session>   (xml);
				gen_parent_route<Pin_control::Session>(xml);
				gen_parent_route<I2c::Session>(xml);
				gen_capture_route(xml);
				gen_parent_rom_route(xml, "fb");
				gen_parent_rom_route(xml, "config", "config -> fb");
				gen_parent_rom_route(xml, "dtb",    "fb.dtb");
				gen_parent_route<Rm_session>(xml);
				gen_common_routes(xml);
			});
		});
	};

	void update(Registry<Child_state> &registry, Board_info const &board_info,
	            Xml_node const &platform)
	{
		bool const use_boot_fb = !board_info.options.suspending &&
		                          board_info.detected.boot_fb &&
		                         !board_info.options.display;

		Fb_name const orig_fb_name = _fb_name();

		if (_boot_fb.constructed() && !use_boot_fb)
			_boot_fb.destruct();

		if (board_info.options.display)
			_launcher_fb = board_info.options.display_name;

		error("fb driver update ",
		      use_boot_fb ? " use_boot" : " no use_boot", " ", _fb_name());

		Affinity::Location const fb_affinity =
			board_info.soc.fb_on_dedicated_cpu ? Affinity::Location { 1, 0, 1, 1 }
			                                   : Affinity::Location { };

		_soc_fb.conditional(board_info.soc.fb && board_info.options.display,
		                    registry, Child_state::Attr {
		                        .name      = "fb",
		                        .priority  = Priority::MULTIMEDIA,
		                        .cpu_quota = 20,
		                        .location  = fb_affinity,
		                        .initial   = { Ram_quota { 16*1024*1024 },
		                                       Cap_quota { 250 } },
		                        .max       = { } } );

		if (use_boot_fb && !_boot_fb.constructed())
			Boot_fb::with_mode(platform, [&] (Boot_fb::Mode mode) {
				_boot_fb.construct(registry, "boot_fb", Priority::MULTIMEDIA,
				                   mode.ram_quota(), Cap_quota { 100 }); });

		if (orig_fb_name != _fb_name()) {
			Session_label label { "report -> runtime/", _fb_name(), "/connectors" };
			if (_fb_name().length() > 1)
				_connectors.construct(_env, label,
				                      *this, &Fb_driver::_handle_connectors);
			else
				_connectors.destruct();
		}
	}

	static bool suspend_supported(Board_info const &board_info)
	{
		/* offer suspend/resume only when using intel graphics */
		return board_info.detected.intel_gfx
		   && !board_info.options.suppress.intel_gpu;
	}

	void with_connectors(auto const &fn) const
	{
		if (_connectors.constructed())
			_connectors->with_xml(fn);
	}
};

#endif /* _DRIVER__FB_H_ */
