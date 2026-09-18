/*
 * \brief  Usb multiplexing component - session component (dev clients)
 * \author Stefan Kalkowski
 * \date   2026-08-05
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__SERVER__USB__SESSION_COMPONENT_H_
#define _SRC__SERVER__USB__SESSION_COMPONENT_H_

/* Genode */
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <base/session_object.h>
#include <os/dynamic_rom_session.h>
#include <os/session_policy.h>

/* local includes */
#include <device_component.h>

namespace Usb {
	class Root;
	class Session_component;
}


class Usb::Session_component
:
	public  Session_object<Session>,
	private Id_space<Session_component>::Element,
	private Registry<Session_component>::Element,
	private Dynamic_rom_session::Producer
{
	private:

		friend class Device_component;
		friend class Interface_component;

		Env &_env;

		Root &_root;

		Attached_rom_dataspace &_config;

		Device_dictionary &_device_dict;

		Request_id_allocator &_request_id_alloc;

		Accounted_ram_allocator _ram { _env.ram(), _ram_quota_guard(),
		                               _cap_quota_guard()  };

		Heap _heap { _ram, _env.rm() };

		using Session_allocator =
			Memory::Constrained_obj_allocator<Device_component>;

		Session_allocator _dev_session_alloc { _heap };

		Dynamic_rom_session _rom_session { _env.ep(), _ram, _env.rm(), *this };

		Signal_context_capability _release_sigh_cap {};

		Registry<Device_component> _device_registry {};

		/* Dummy signal used for non-existent (vanished) devices/ifaces */
		Signal_handler<Session_component> _dummy_handler {
			_env.ep(), *this, &Session_component::_do_nothing };

		enum class State { CREATED, RELEASING };
		State _state { State::CREATED };

		void _do_nothing() {}

		bool _matches(Device const &dev) const;
		bool _matches(Device const &dev, uint8_t iface) const;

	public:

		/*
		 * Noncopyable
		 */
		Session_component(Session_component const &) = delete;
		Session_component &operator = (Session_component const &) = delete;

		using Server_id_space = Id_space<Session_component>;

		Session_component(Env                         &env,
		                  Root                        &root,
		                  Device_dictionary           &dict,
		                  Resources             const &resources,
		                  Label                 const &label,
		                  Server_id_space::Id   const  id,
		                  Server_id_space             &id_space,
		                  Registry<Session_component> &registry,
		                  Attached_rom_dataspace      &config,
		                  Request_id_allocator        &id_alloc);

		using Server_id_space::Element::id;

		void update_devices();
		void update_policy();

		bool acquired(Device const &) const;

		Release_result release_all();

		void released(Device_component &dc);


		/***************************
		 ** USB session interface **
		 ***************************/

		Rom_session_capability devices_rom() override;
		Acquisition_result acquire_device(Device_name const &name) override;
		Acquisition_result acquire_single_device() override;
		Release_result release_device(Device_capability) override;
		void release_sigh(Signal_context_capability) override;


		/***************************************
		 ** Dynamic_rom_session::Producer API **
		 ***************************************/

		void generate(Generator &) override;
};

#endif /* _SRC__SERVER__USB__SESSION_COMPONENT_H_ */
