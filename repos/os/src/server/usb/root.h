/*
 * \brief  Usb multiplexing component - root interface
 * \author Stefan Kalkowski
 * \date   2026-08-05
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__SERVER__USB__ROOT_H_
#define _SRC__SERVER__USB__ROOT_H_

/* Genode */
#include <base/service.h>
#include <base/session_state.h>

/* local includes */
#include <host_session_component.h>
#include <session_component.h>

namespace Usb { class Root; }


class Usb::Root
{
	private:

		Env &_env;

		Attached_rom_dataspace &_config;

		Device_dictionary &_device_dict;

		Device_model::Update_handler &_update_handler;

		Sliced_heap _sliced_heap { _env.ram(), _env.rm() };

		Memory::Constrained_obj_allocator<Session_component>
			_session_alloc { _sliced_heap };

		Memory::Constrained_obj_allocator<Host_session_component>
			_host_session_alloc { _sliced_heap };

		Attached_rom_dataspace _session_requests { _env, "session_requests" };

		Signal_handler<Root> _session_request_handler {
			_env.ep(), *this, &Root::_handle_session_requests };

		Request_id_allocator _request_id_alloc {};

		Registry<Session_component> _registry {};

		Id_space<Session_component> _session_id_space { };

		Id_space<Host_session_component> _host_session_id_space { };

		Parent::Session_response _create(Parent::Server::Id const id,
		                                 auto const &fn);

		struct Ignore {};
		using Request_result = Attempt<Parent::Session_response, Ignore>;

		Request_result _handle_session_request(Node const &,
		                                       Parent::Server::Id const);

		void _handle_session_requests();

	public:

		/*
		 * Noncopyable
		 */
		Root(Root const &) = delete;
		Root &operator = (Root const &) = delete;

		Root(Env &env, Attached_rom_dataspace &config, Device_dictionary &dict,
		     Device_model::Update_handler &handler);

		void announce();

		void update_devices();

		void update_policy();

		bool acquired(Device const &) const;

		void released(Session_component&);

		Id_space<Host_session_component> &host_id_space() {
			return _host_session_id_space; }

		void device_state_changed() {
			_update_handler.model_updated(); }
};

#endif /* _SRC__SERVER__USB__ROOT_H_ */
