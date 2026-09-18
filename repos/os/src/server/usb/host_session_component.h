/*
 * \brief  Usb multiplexing component - session component (host controller)
 * \author Stefan Kalkowski
 * \date   2026-08-06
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__SERVER__USB__HOST_SESSION_COMPONENT_H_
#define _SRC__SERVER__USB__HOST_SESSION_COMPONENT_H_

/* Genode */
#include <base/attached_ram_dataspace.h>
#include <base/heap.h>
#include <base/id_space.h>
#include <base/session_object.h>
#include <usb_session/usb_session.h>
#include <util/bit_allocator.h>

/* local includes */
#include <device.h>

namespace Usb {
	class Host_session_component;
	class Host_root;
}


class Usb::Host_session_component
:
	public  Session_object<Host_session>,
	private Id_space<Host_session_component>::Element
{
	public:

		class Request;

		using Request_id = Id_space<Request>::Id;

	private:

		friend class Host_root;

		Env &_env;

		Accounted_ram_allocator _ram { _env.ram(), _ram_quota_guard(),
		                               _cap_quota_guard()  };

		Heap _heap { _ram, _env.rm() };

		enum {
			DEVICES_DS_SIZE  = 128 * 1024,
			REQUESTS_DS_SIZE = 4 * 1024
		};

		Attached_ram_dataspace _client_dev_ds { _ram, _env.rm(),
		                                        DEVICES_DS_SIZE };

		Attached_ram_dataspace _requests_ds { _ram, _env.rm(),
		                                      REQUESTS_DS_SIZE };

		Signal_context_capability _requests_sigh_cap { };

		Device_model _devices;

		Signal_context_capability _sigh_cap { };

		Id_space<Request> _requests {};

	public:

		/*
		 * Noncopyable
		 */
		Host_session_component(Host_session_component const &) = delete;
		Host_session_component &operator = (Host_session_component const &) = delete;

		using Server_id_space = Id_space<Host_session_component>;

		Host_session_component(Env                          &env,
		                       Device_dictionary            &dict,
		                       Resources              const &resources,
		                       Label                  const &label,
		                       Server_id_space::Id    const  id,
		                       Server_id_space              &id_space,
		                       Device_model::Update_handler &handler);

		~Host_session_component();

		Signal_context_capability sink_sigh() const {
			return _sigh_cap; }

		void update_requests();

		using Id_space<Host_session_component>::Element::id;


		/********************************
		 ** USB host session interface **
		 ********************************/

		Dataspace_capability devices_dataspace() override {
			return _client_dev_ds.cap(); }

		Submit_result devices_submit(size_t size) override;
		Rom_dataspace_capability requests_rom() override;
		void requests_sigh(Signal_context_capability) override;

		void sigh(Signal_context_capability sigh) override {
			_sigh_cap = sigh; }

		Dataspace_result client_dataspace(Id) override;
		Signal_result client_sigh(Id) override;
		void acquired(Id) override;
		void released(Id) override;
};


class Usb::Host_session_component::Request
:
	private Id_space<Request>::Element, Interface
{
	public:

		enum class Type { ACQUIRE, RELEASE };

		Type const type;

		Dataspace_capability const ds_cap;

		Signal_context_capability const sigh_cap;

		Request(Host_session_component &, Type,
		        Dataspace_capability, Signal_context_capability);

		Request(Host_session_component &host, Request_id id, Type type,
		        Dataspace_capability ds_cap, Signal_context_capability sigh)
		:
			Id_space<Request>::Element(*this, host._requests, id),
			type(type), ds_cap(ds_cap), sigh_cap(sigh)
		{ }

		virtual void generate(Generator &) const = 0;
		virtual void done() = 0;

		using Id_space<Request>::Element::id;
};

#endif /* _SRC__SERVER__USB__HOST_SESSION_COMPONENT_H_ */
