/*
 * \brief  Usb multiplexing component - interface component (clients)
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
#include <interface_component.h>
#include <host_session_component.h>
#include <root.h>
#include <session_component.h>

using namespace Usb;

Interface_component::Request::Request(Host_session_component &host_session,
                                      Type type,
                                      Host_session_component::Request_id dev_id,
                                      uint8_t iface_idx,
                                      Interface_component &ic)
:
	Host_session_component::Request(host_session, ic.id(), type,
	                                ic._tx.dataspace(),
	                                ic._source_sigh_cap),
	device_id(dev_id), iface_idx(iface_idx),
	session(ic)
{
	host_session.update_requests();
}


void Interface_component::Request::generate(Generator &g) const
{
	bool const acquire = (type == Type::ACQUIRE);

	g.node(acquire ? "acquire" : "release",
		[&] {
			g.attribute("name",      id().value);
			g.attribute("device_id", device_id.value);
			g.attribute("interface", iface_idx);

			if (acquire && session._device_session._dma.constructed() &&
			    session._state != Interface_component::State::UNAVAIL) {
				addr_t dma_addr = ~0UL;

				session._tx.dma_result().with_result(
					[&] (auto cap) {
						dma_addr = session._device_session._dma->bus_addr(cap);
					}, [] (auto) {});

				g.attribute("dma_addr", dma_addr);
			}
	});
}


void Interface_component::Request::done()
{
	if (type == Type::ACQUIRE)
		session._state = Interface_component::State::ACQUIRED;

	bool const released = (type == Type::RELEASE);

	if (!released) session._request.destruct();

	if (released) session._device_session.released(session);
}


void Interface_component::acquire(Signal_context_capability sigh)
{
	_source_sigh_cap = sigh;

	if (_state != State::CREATED || _request.constructed())
		return;

	auto &dict = _device_session._session._device_dict;

	dict.with_element(_device_session._device_id,
		[&] (Device &device) {
			_request.construct(device.host_drv, Request::Type::ACQUIRE,
			                   _device_session.id(), _iface_idx, *this);
		},

		/* device not found, removed in the meantime */
		[&] { });
}


Device_session::Release_result Interface_component::release()
{
	using Result = Device_session::Release_result;

	if (_state == State::CREATED || _state == State::UNAVAIL)
		return Result::OK;

	if (_state == State::RELEASING)
		return Result::PENDING;

	auto &id_space = _device_session._session._root.host_id_space();
	auto host_id = _device_session._device_id.host_id;

	return id_space.apply<Host_session_component>(host_id,
		[&] (Host_session_component &hsc) {
			_request.construct(hsc, Request::Type::RELEASE,
			                   _device_session.id(), _iface_idx, *this);
			return Result::PENDING;
		},

		/* device not found, removed in the meantime */
		[&] {
			return Result::OK;
		});
}


Interface_component::Interface_component(Env &env, Device_component &dc,
                                         Device &device, uint8_t iface_idx,
                                         size_t buffer_size)
:
	Registry<Interface_component>::Element(dc._iface_registry, *this),
	_env(env),
	_id_allocation(dc._session._request_id_alloc.alloc()),
	_device_session(dc), _iface_idx(iface_idx),
	_tx(env, *this, dc._alloc_dma(device.host_drv.label(), buffer_size),
	    device.host_drv.sink_sigh()),
	_state(State::CREATED)
{
	_env.ep().manage(*this);
}


Interface_component::Interface_component(Env &env, Device_component &dc,
                                         size_t buffer_size)
:
	Registry<Interface_component>::Element(dc._iface_registry, *this),
	_env(env),
	_id_allocation(Request_id_allocator::Error::DENIED),
	_device_session(dc), _iface_idx(0),
	_tx(env, *this, dc._alloc_ram(buffer_size),
	    dc._session._dummy_handler),
	_state(State::UNAVAIL)
{
	_env.ep().manage(*this);
}


Interface_component::~Interface_component()
{
	_id_allocation.with_result(
		[&] (auto v) { _device_session._session._request_id_alloc.free(v); },
		[]  (auto)   { /* nothing to free */ });

	_env.ep().dissolve(*this);

	_tx.dma_result().with_result(
		[&] (auto cap) {
			if (_state == State::UNAVAIL) _device_session._free_ram(cap);
			else _device_session._free_dma(cap);
		}, [] (auto) {});
}
