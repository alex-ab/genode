/*
 * \brief  Client-side Platform-session interface
 * \author Stefan Kalkowski
 * \date   2020-04-28
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__PLATFORM_SESSION__CLIENT_H_
#define _INCLUDE__PLATFORM_SESSION__CLIENT_H_

#include <base/rpc_client.h>
#include <platform_session/capability.h>

namespace Platform { struct Client; }


struct Platform::Client : Genode::Rpc_client<Session>
{
	Client(Session_capability session)
	: Rpc_client<Session>(session) { }

	Rom_session_capability devices_rom() override {
		return call<Rpc_devices_rom>(); }

	Acquisition_result acquire_device(Device_name const &name) override {
		return call<Rpc_acquire_device>(name); }

	Acquisition_result acquire_single_device() override {
		return call<Rpc_acquire_single_device>(); }

	void release_device(Capability<Device_interface> device) override {
		call<Rpc_release_device>(device); }
};

#endif /* _INCLUDE__PLATFORM_SESSION__CLIENT_H_ */
