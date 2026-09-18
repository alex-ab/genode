/*
 * \brief  Usb multiplexing component - device component (clients)
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
#include <device_component.h>
#include <host_session_component.h>
#include <root.h>
#include <session_component.h>

using namespace Usb;

Device_component::Request::Request(Host_session_component &host_session,
                                   Type type, Device_id::Name name,
                                   Device_component &dc)
:
	Host_session_component::Request(host_session, dc.id(), type,
	                                dc._tx.dataspace(), dc._source_sigh_cap),
	device_name(name),
	session(dc)
{
	host_session.update_requests();
}


void Device_component::Request::generate(Generator &g) const
{
	bool const acquire = (type == Type::ACQUIRE);

	g.node(acquire ? "acquire" : "release",
		[&] {
			g.attribute("name",   id().value);
			g.attribute("device", device_name);

			if (acquire && session._dma.constructed()) {
				addr_t dma_addr = ~0UL;

				session._tx.dma_result().with_result(
					[&] (auto cap) {
						dma_addr = session._dma->bus_addr(cap);
					}, [] (auto) {});

				g.attribute("dma_addr", dma_addr);
			}
	});
}


void Device_component::Request::done()
{
	bool const released = (type == Type::RELEASE);

	session._state = released ? Device_component::State::DISCONNECTED
	                          : Device_component::State::ACQUIRED;

	auto &dc = session;

	if (!released) {
		session._request.destruct();
		dc._session._root.device_state_changed();
		return;
	}

	if (dc._iface_registry.if_empty([] { return true; },
	                                [] { return false; }))
		dc._session.released(dc);
}


Dma_alloc_result Device_component::_alloc_dma(Session_label label, size_t size)
{
	if (!_dma.constructed())
		_dma.construct(_env, label, _session._ram_quota_guard(),
		               _session._cap_quota_guard());

	return _dma->constructed.convert<Dma_alloc_result>(
		[&] (auto) { return _dma->try_alloc(size); },
		[] (auto err) { return err; });
}


Dma_alloc_result Device_component::_alloc_ram(size_t size)
{
	return _session._ram.try_alloc(size).convert<Dma_alloc_result>(
		[] (auto &a) -> Dma_alloc_result {
			a.deallocate = false;
			return a.cap;
		},
		[] (Alloc_error e) -> Dma_alloc_result { return e; });
}


void Device_component::_free_dma(Ram_dataspace_capability cap)
{
	if (_dma.constructed()) _dma->free(cap);
}


void Device_component::_free_ram(Ram_dataspace_capability cap)
{
	_session._ram.free(cap);
}


void Device_component::acquire(Signal_context_capability sigh)
{
	_source_sigh_cap = sigh;

	if (_state != State::CREATED || _request.constructed())
		return;

	_session._device_dict.with_element(_device_id,
		[&] (Device &device) {
			_request.construct(device.host_drv, Request::Type::ACQUIRE,
			                   device.name(), *this);
		},

		/* device not found, removed in the meantime */
		[&] { });
}


Device_session::Acquisition_result
Device_component::acquire_interface(uint8_t iface, size_t size)
{
	Session_allocator::Result result =
		_session._device_dict.with_element(_device_id,
			[&] (Device &dev) {
				return (!_session._matches(dev, iface))
					? _iface_session_alloc.create(_env, *this, size)
					: _iface_session_alloc.create(_env, *this, dev, iface,
					                              size);
			},
			[&] { return _iface_session_alloc.create(_env, *this, size); });

	using Result = Acquisition_result;
	using Error  = Acquisition_error;

	auto convert_error = [] (auto err) -> Result {
		if (err == decltype(err)::OUT_OF_RAM)
			return Error::OUT_OF_RAM;
		if (err == decltype(err)::OUT_OF_CAPS)
			return Error::OUT_OF_CAPS;
		return Result(Interface_capability());
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
			[&]  (auto e) { return convert_error(e); });
}


Device_session::Release_result
Device_component::release_interface(Interface_capability iface_cap)
{
	Release_result result = Release_result::OK;

	_iface_registry.for_each(
		[&] (auto &ic) {
			if (ic.cap().local_name() != iface_cap.local_name())
				return;

			if (ic.release() == Release_result::PENDING)
				result = Release_result::PENDING;
			else
				_iface_session_alloc.destroy(ic);
		});

	return result;
}


Device_session::Release_result Device_component::release()
{
	if (_state == State::CREATED || _state == State::UNAVAIL)
		return Release_result::OK;

	if (_state == State::RELEASING)
		return Release_result::PENDING;

	_state = State::RELEASING;

	Release_result result = Release_result::OK;

	_iface_registry.for_each(
		[&] (auto &ic) {
			if (ic.release() == Release_result::PENDING)
				result = Release_result::PENDING;
			else
				_iface_session_alloc.destroy(ic);
		});

	auto &id_space = _session._root.host_id_space();

	return id_space.apply<Host_session_component>(_device_id.host_id,
		[&] (Host_session_component &hsc) {
			_request.construct(hsc, Request::Type::RELEASE,
			                   _device_id.name, *this);
			return Release_result::PENDING;
		},

		/* device not found, removed in the meantime */
		[&] {
			return result;
		});
}


void Device_component::released(Interface_component &ic)
{
	_iface_session_alloc.destroy(ic);

	if (_release_sigh_cap.valid())
		Signal_transmitter(_release_sigh_cap).submit();

	if (_state == State::DISCONNECTED)
		_iface_registry.if_empty([&] { _session.released(*this); }, [] {});
}


void Device_component::release_sigh(Signal_context_capability sigh)
{
	_release_sigh_cap = sigh;
}


Device_component::Device_component(Env &env, Session_component &session,
                                   Device &device)
:
	Registry<Device_component>::Element(session._device_registry, *this),
	_env(env),
	_session(session),
	_id_allocation(session._request_id_alloc.alloc()),
	_device_id(device.id()),
	_iface_session_alloc(session._heap),
	_tx(env, *this, _alloc_dma(device.host_drv.label(), BUFFER_SIZE),
	    device.host_drv.sink_sigh()),
	_state(State::CREATED)
{
	if (_id_allocation.failed()) _state = State::UNAVAIL;
	_env.ep().manage(*this);
}


Device_component::Device_component(Env &env, Session_component &session)
:
	Registry<Device_component>::Element(session._device_registry, *this),
	_env(env),
	_session(session),
	_id_allocation(Request_id_allocator::Error::DENIED),
	_device_id(),
	_iface_session_alloc(session._heap),
	_tx(env, *this, _alloc_ram(BUFFER_SIZE),
	    session._dummy_handler),
	_state(State::UNAVAIL)
{
	_env.ep().manage(*this);
}


Device_component::~Device_component()
{
	_id_allocation.with_result(
		[&] (auto v) { _session._request_id_alloc.free(v); },
		[]  (auto)   { /* nothing to free */ });

	_env.ep().dissolve(*this);

	_tx.dma_result().with_result(
		[&] (auto cap) {
			if (_state == State::UNAVAIL) _free_ram(cap);
			else _free_dma(cap);
		}, [] (auto) {});
}
