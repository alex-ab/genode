/*
 * \brief  Usb multiplexing component - session component (clients)
 * \author Stefan Kalkowski
 * \date   2026-08-05
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* local includes */
#include <root.h>
#include <session_component.h>

using namespace Genode;
using namespace Usb;

bool Session_component::_matches(Device const &dev) const
{
	return with_matching_policy(label(), _config.node(), [&] (Node const &policy) {

		bool match = false;
		policy.for_each_sub_node("device", [&] (Node const &node) {
			uint16_t vendor  = node.attribute_value<uint16_t>("vendor_id", 0);
			uint16_t product = node.attribute_value<uint16_t>("product_id", 0);

			using Name = String<Device_id::size()>;
			Name name = node.attribute_value("name", Name());

			if (((vendor == dev.vendor_id.value) &&
			     (product == dev.product_id.value)) || (dev.id() == name)) {
				match = true;
				return;
			}

			enum { CLASS_AUDIO = 0x1, CLASS_HID = 0x3, CLASS_VIDEO = 0xe };
			uint8_t iclass = node.attribute_value<uint8_t>("class", 0);
			bool found_av  = false;
			dev.for_each_config([&] (auto const &cfg) {
				if (!cfg.active)
					return;
				cfg.for_each_interface([&] (auto const &iface) {
					if (iface.iclass.value == iclass)      match    = true;
					if (iface.iclass.value == CLASS_AUDIO) found_av = true;
					if (iface.iclass.value == CLASS_VIDEO) found_av = true;
				});
			});

			/* do not match HID in AUDIO/VIDEO devices */
			if (match && iclass == CLASS_HID && found_av) match = false;
		});

		return match;
	}, [] { return false; });
}


bool Session_component::_matches(Device const &dev, uint8_t) const
{
	/*
	 * For now, we do not have implemented control per interface
	 * policies, therefore just return whether access to the
	 * device is allowed
	 */
	return _matches(dev);
}


Rom_session_capability Session_component::devices_rom()
{
	return _rom_session.cap();
}


Session_component::Acquisition_result
Session_component::acquire_device(Device_name const &name)
{
	Session_allocator::Result result = _device_dict.with_element(name,
		[&] (Device &dev) {
			return (!_matches(dev))
				? _dev_session_alloc.create(_env, *this)
				: _dev_session_alloc.create(_env, *this, dev);
		},
		[&] { return _dev_session_alloc.create(_env, *this); });

	using Result = Acquisition_result;
	using Error  = Acquisition_error;

	auto convert_error = [] (auto err) -> Result {
		if (err == decltype(err)::OUT_OF_RAM)
			return Error::OUT_OF_RAM;
		if (err == decltype(err)::OUT_OF_CAPS)
			return Error::OUT_OF_CAPS;
		return Result(Device_capability());
	};

	return result.convert<Result>(
			[&] (auto &a) {
				return a.obj.constructed().template convert<Result>(
					[&] (auto) {
						a.deallocate = false;
						return a.obj.cap();
					},
					[&] (auto e) { return convert_error(e); });
			},
			[&] (auto e) { return convert_error(e); });
}


Session_component::Acquisition_result
Session_component::acquire_single_device()
{
	bool const empty = _device_registry.if_empty([] { return true; },
	                                             [] { return false; });

	if (!empty)
		return Acquisition_result(Device_capability());

	auto name = Device_name();

	_device_dict.for_each([&] (auto &dev) {
		if (!name.valid() && _matches(dev))
			name = dev.id();
	});

	if (name.valid()) {
		/*
		 * For now, check whether device is acquired to stay consistent
		 * with former behaviour of acquire_single_device. Later we need
		 * to allow double acquisition by acquire_device *and*
		 * acquire_single_device.
		 */
		bool const avail = _device_dict.with_element(name,
			[&] (Device &dev) { return !_root.acquired(dev); },
			[&] { return false; });
		if (avail)
			return acquire_device(name);
	}

	return Acquisition_result(Device_capability());
}


Session_component::Release_result
Session_component::release_device(Device_capability cap)
{
	Release_result result = Release_result::OK;

	_device_registry.for_each(
		[&] (auto &dc) {
			if (dc.cap().local_name() != cap.local_name())
				return;

			if (dc.release() == Device_session::Release_result::PENDING)
				result = Release_result::PENDING;
			else
				_dev_session_alloc.destroy(dc);
		});

	return result;
}


Session_component::Release_result Session_component::release_all()
{
	_state = State::RELEASING;

	Release_result result = Release_result::OK;

	_device_registry.for_each(
		[&] (auto &dc) {
			if (dc.release() == Device_session::Release_result::PENDING)
				result = Release_result::PENDING;
			else
				_dev_session_alloc.destroy(dc);
		});

	return result;
}


void Session_component::released(Device_component &dc)
{
	_dev_session_alloc.destroy(dc);

	_root.device_state_changed();

	if (_release_sigh_cap.valid())
		Signal_transmitter(_release_sigh_cap).submit();

	if (_state != State::RELEASING)
		return;

	if (_device_registry.if_empty([] { return true;  },
	                              [] { return false; }))
		_root.released(*this);
}


void Session_component::release_sigh(Signal_context_capability cap)
{
	_release_sigh_cap = cap;
}


void Session_component::generate(Generator &g)
{
	_device_dict.for_each([&] (auto const &dev) {
		if (_matches(dev))
			dev.generate(g, acquired(dev));
	});
}


void Session_component::update_devices()
{
	_rom_session.trigger_update();
}


void Session_component::update_policy()
{
	_device_dict.for_each([&] (auto const &dev) {
		_device_registry.for_each([&] (auto &session) {
			if (session.acquired(dev) && !_matches(dev))
				session.release();
		});
	});

	update_devices();
}


bool Session_component::acquired(Device const &dev) const
{
	bool acquired = false;
	_device_registry.for_each([&] (auto const &session) {
		if (!acquired) acquired = session.acquired(dev); });
	return acquired;
}


Session_component::Session_component(Env                         &env,
                                     Root                        &root,
                                     Device_dictionary           &dict,
                                     Resources             const &resources,
                                     Label                 const &label,
                                     Server_id_space::Id   const  id,
                                     Server_id_space             &id_space,
                                     Registry<Session_component> &registry,
                                     Attached_rom_dataspace      &config,
                                     Request_id_allocator        &id_alloc)
:
	Session_object<Usb::Session>(env.ep(), resources, label),
	Id_space<Session_component>::Element(*this, id_space, {id.value}),
	Registry<Session_component>::Element(registry, *this),
	Dynamic_rom_session::Producer("devices"),
	_env(env), _root(root), _config(config), _device_dict(dict),
	_request_id_alloc(id_alloc)
{}
