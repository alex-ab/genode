/*
 * \brief  Usb multiplexing component
 * \author Stefan Kalkowski
 * \date   2026-08-05
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode */
#include <base/component.h>
#include <base/heap.h>
#include <os/reporter.h>

/* local includes */
#include <root.h>

namespace Usb { class Main; }


class Usb::Main : public Device_model::Update_handler
{
	private:

		Env &_env;

		enum class State { STARTED, ANNOUNCED };

		State _state { State::STARTED };

		Attached_rom_dataspace _config { _env, "config" };

		Signal_handler<Main> _config_handler { _env.ep(), *this,
		                                       &Main::_handle_config };

		Constructible<Expanding_reporter> _config_reporter { };
		Constructible<Expanding_reporter> _device_reporter { };

		Device_dictionary _device_dict {};

		Root _root { _env, _config, _device_dict, *this };

		void _handle_config();
		void _announce_service();

	public:

		/*
		 * Noncopyable
		 */
		Main(Main const &) = delete;
		Main &operator = (Main const &) = delete;

		Main(Env&);

		virtual ~Main() {}


		/******************************
		 ** Update_handler interface **
		 ******************************/

		void model_updated() override;
};


void Usb::Main::_handle_config()
{
	_config.update();

	/*
	 * Check for report policy, and resp. con-/destruct reporter
	 */
	_config.node().with_optional_sub_node("report", [&] (Node const &node) {
		_device_reporter.conditional(node.attribute_value("devices", false),
		                             _env, "devices", "devices" );
		_config_reporter.conditional(node.attribute_value("config", false),
		                             _env, "config");
	});

	/*
	 * Report the own configuration to show management component
	 * that we've consumed the configuration
	 */
	if (_config_reporter.constructed())
		_config_reporter->generate([&] (Generator &g) {
			g.node_attributes(_config.node());
			g.append_node_content(_config.node());
		});

	/*
	 * Defer the announcement of the device client service until the first
	 * valid configuration becomes available. This is needed in scenarios where
	 * the configuration is dynamically generated and supplied to the USB
	 * multiplexer via the report-ROM service.
	 */
	_announce_service();

	_root.update_policy();
}


void Usb::Main::_announce_service()
{
	if (_state != State::STARTED)
		return;

	if (_config.node().type() == "config") {
		_root.announce();
		_state = State::ANNOUNCED;
	}
}


void Usb::Main::model_updated()
{
	_root.update_devices();

	if (!_device_reporter.constructed())
		return;

	_device_reporter->generate([&] (Generator &g) {
		_device_dict.for_each([&] (auto const &dev) {
			dev.generate(g, _root.acquired(dev));
		});
	});
}


Usb::Main::Main(Env &env)
:
	_env(env)
{
	_config.sigh(_config_handler);
	_handle_config();
}


void Component::construct(Genode::Env &env) {
	static Usb::Main main(env); }
