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

#include <device_component.h>
#include <host_session_component.h>

using namespace Usb;

void Host_session_component::update_requests()
{
	Byte_range_ptr bytes(_requests_ds.local_addr<char>(), _requests_ds.size());
	Generator::generate(bytes, "requests", [&] (Generator &g) {
		_requests.for_each<Request>([&] (auto const &req) { req.generate(g); });
	}).with_result(
		[&] (size_t) {
			Signal_transmitter(_requests_sigh_cap).submit();
		},
		[&] (auto) {
			Genode::error("Requests report exceeds buffer!");
		});

}


Host_session::Submit_result Host_session_component::devices_submit(size_t size)
{
	size_t const num_bytes = min(_client_dev_ds.size(), size);

	auto node = Const_byte_range_ptr(_client_dev_ds.local_addr<char>(),
	                                 num_bytes);

	try {
		_devices.update(_heap, node);
	} catch (Out_of_ram) {
		return Alloc_error::OUT_OF_RAM;
	} catch (Out_of_caps) {
		return Alloc_error::OUT_OF_CAPS;
	}

	return Ok();
}


Rom_dataspace_capability Host_session_component::requests_rom()
{
	Dataspace_capability cap = _requests_ds.cap();
	return static_cap_cast<Rom_dataspace>(cap);
}


void Host_session_component::requests_sigh(Signal_context_capability sigh)
{
	_requests_sigh_cap = sigh;
}


Host_session::Dataspace_result Host_session_component::client_dataspace(Id id)
{
	return _requests.apply<Request>({id.value},
		[&] (Request &request) -> Dataspace_result {
			return request.ds_cap;
		},
		[] () -> Dataspace_result { return Id_error::INVALID; });
}


Host_session::Signal_result Host_session_component::client_sigh(Id id)
{
	return _requests.apply<Request>({id.value},
		[&] (Request const &request) -> Signal_result {
			return request.sigh_cap;
		},
		[] () -> Signal_result { return Id_error::INVALID; });
}


void Host_session_component::acquired(Id id)
{
	_requests.apply<Request>({id.value},
		[&] (Request &request) { request.done(); },
		[] { });

	update_requests();
}


void Host_session_component::released(Id id)
{
	_requests.apply<Request>({id.value},
		[&] (Request &request) { request.done(); },
		[] { });

	update_requests();
}


Host_session_component::Host_session_component(Env                          &env,
                                               Device_dictionary            &dict,
                                               Resources              const &resources,
                                               Label                  const &label,
                                               Server_id_space::Id    const  id,
                                               Server_id_space              &id_space,
                                               Device_model::Update_handler &handler)
:
	Session_object<Usb::Host_session>(env.ep(), resources, label),
	Id_space<Host_session_component>::Element(*this, id_space, {id.value}),
	_env(env), _devices(dict, *this, handler)
{
	_env.pd().seal_ram(_requests_ds.cap());
}


Host_session_component::~Host_session_component()
{
	/* delete device model */
	_devices.update(_heap, Node());
}
