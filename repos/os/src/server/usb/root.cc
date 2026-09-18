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

/* local includes */
#include <root.h>

using namespace Usb;

static Session_label label_correction(Session_label label)
{
	Session_label result = label;

	label.with_span([&] (Span label) {
		String<8> separator { "->" };
		separator.with_span([&] (Span suffix) {
			if (label.ends_with(suffix))
				result = { label, " " };
		});
	});

	return result;
}


void Usb::Root::_handle_session_requests()
{
	_session_requests.update();

	Node const &requests = _session_requests.node();

	requests.for_each_sub_node([&] (Node const &request) {
		if (!request.has_attribute("id"))
			return;

		Parent::Server::Id const id { request.attribute_value("id", 0UL) };

		_handle_session_request(request, id).with_result(
			[&] (auto response) {
				_env.parent().session_response(id, response);
			}, [] (auto) {});
	});
}


Usb::Root::Request_result
Usb::Root::_handle_session_request(Node const &request, Parent::Server::Id id)
{
	if (request.has_type("close"))
		return _host_session_id_space.apply<Host_session_component>({id.value},
			[&] (Host_session_component &host_session) -> Request_result {
				_host_session_alloc.destroy(host_session);
				return Parent::Session_response::CLOSED;
			},
			[&] () -> Request_result {
				return _session_id_space.apply<Session_component>({id.value},
					[&] (Session_component &session) -> Request_result {
						if (session.release_all() == Session::Release_result::OK) {
							_session_alloc.destroy(session);
							return Parent::Session_response::CLOSED;
						}
						return Ignore();
					});
			});

	if (request.has_type("upgrade")) {

		auto upgrade_fn = [&] (auto &session) {
			Ram_quota ram_quota(request.attribute_value("ram_quota", 0UL));
			Cap_quota cap_quota(request.attribute_value("cap_quota", 0UL));
			session.upgrade(ram_quota);
			session.upgrade(cap_quota);
		};

		_host_session_id_space.apply<Host_session_component>({id.value},
			[&] (Host_session_component &host_session) {
				upgrade_fn(host_session); },
			[&] {
				_session_id_space.apply<Session_component>({id.value},
					[&] (Session_component &session) { upgrade_fn(session); });
			});

		return Parent::Session_response::OK;
	}

	if (request.has_type("create")) {

		Service::Name const name {
			request.attribute_value("service", Service::Name()) };

		if (!request.has_sub_node("args"))
			return Parent::Session_response::DENIED;

		using Args = Session_state::Args;

		Args const args = request.with_sub_node("args",
			[&] (Node const &node) { return Args(Node::Quoted_content(node)); },
			[]                     { return Args(); });

		Session_label const label {
			label_correction(request.attribute_value("label",
			                                         Session_label::String())) };

		Ram_quota const ram_quota = ram_quota_from_args(args.string());
		Cap_quota const cap_quota = cap_quota_from_args(args.string());

		bool const host_session = name == Host_session::service_name();

		size_t obj_size = host_session ? sizeof(Host_session_component)
		                               : sizeof(Session_component);
		size_t const needed_ram = obj_size + _sliced_heap.overhead(obj_size);
		size_t const needed_cap = 1;

		if (needed_ram > ram_quota.value)
			return Parent::Session_response::INSUFFICIENT_RAM;

		if (needed_cap > cap_quota.value)
			return Parent::Session_response::INSUFFICIENT_CAPS;

		Session::Resources resources { { ram_quota.value - needed_ram },
			                           { cap_quota.value - needed_cap } };

		if (name == Session::service_name())
			return _create(id, [&] {
				Id_space<Session_component>::Id local_id { id.value };
				return _session_alloc.create(_env, *this, _device_dict,
				                             resources, label, local_id,
				                             _session_id_space, _registry,
				                             _config, _request_id_alloc);
			});

		if (name == Host_session::service_name())
			return _create(id, [&] {
				Id_space<Host_session_component>::Id local_id { id.value };
				return _host_session_alloc.create(_env, _device_dict,
				                                  resources, label, local_id,
				                                  _host_session_id_space,
				                                  _update_handler);
			});

		return Parent::Session_response::DENIED;
	}

	/* ignore unknown request */
	return Ignore();
}


Parent::Session_response Usb::Root::_create(Parent::Server::Id const id,
                                            auto const &fn)
{
	using Response = Parent::Session_response;

	try {
		return fn().template convert<Response>(
			[&] (auto &a) -> Response {
				a.deallocate = false;
				_env.parent().deliver_session_cap(id, a.obj.cap());
				return Response::OK;
			},
			[&] (Alloc_error e) {
				switch (e) {
				case Alloc_error::OUT_OF_RAM:  return Response::INSUFFICIENT_RAM;
				case Alloc_error::OUT_OF_CAPS: return Response::INSUFFICIENT_CAPS;
				case Alloc_error::DENIED:      break;
				}
				return Response::DENIED;
			});
	} catch(Out_of_ram) {
		return Response::INSUFFICIENT_RAM;
	} catch(Out_of_caps) {
		return Response::INSUFFICIENT_CAPS;
	} catch(Denied) {
		return Response::DENIED;
	}

	return Response::DENIED;
}


void Usb::Root::update_devices()
{
	_registry.for_each([&] (auto &session) {
		session.update_devices(); });
}


void Usb::Root::update_policy()
{
	_registry.for_each([&] (auto &session) {
		session.update_policy(); });
}


bool Usb::Root::acquired(Device const &dev) const
{
	bool acquired = false;
	_registry.for_each([&] (auto &session) {
		if (!acquired) acquired = session.acquired(dev); });
	return acquired;
}


void Usb::Root::released(Session_component &sc)
{
	auto id = sc.id();
	_session_alloc.destroy(sc);
	_env.parent().session_response({id.value}, Parent::Session_response::CLOSED);
}


void Usb::Root::announce()
{
	_env.parent().announce(Session::service_name());
}


Usb::Root::Root(Env &env, Attached_rom_dataspace &config,
                Device_dictionary &dict, Device_model::Update_handler &handler)
:
	_env(env), _config(config), _device_dict(dict), _update_handler(handler)
{
	_session_requests.sigh(_session_request_handler);

	_env.parent().announce(Host_session::service_name());

	_handle_session_requests();
}
