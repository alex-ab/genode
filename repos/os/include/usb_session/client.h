/**
 * \brief  USB session client implementation
 * \author Sebastian Sumpf
 * \author Stefan Kalkowski
 * \date   2014-12-08
 */

/*
 * Copyright (C) 2014-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */
#ifndef _INCLUDE__USB_SESSION__CLIENT_H_
#define _INCLUDE__USB_SESSION__CLIENT_H_

#include <base/rpc_client.h>

#include <usb_session/usb_session.h>

namespace Usb { struct Client; }


struct Usb::Client : Genode::Rpc_client<Session>
{
	Client(Session_capability session)
	: Rpc_client<Session>(session) { }

	Rom_session_capability devices_rom() override {
		return call<Rpc_devices_rom>(); }

	Acquisition_result acquire_device(Device_name const &name) override {
		return call<Rpc_acquire_device>(name); }

	Acquisition_result acquire_single_device() override {
		return call<Rpc_acquire_single_device>(); }

	void release_sigh(Signal_context_capability sigh) override {
		call<Rpc_release_sigh>(sigh); }

	Release_result release_device(Device_capability device) override {
		return call<Rpc_release_device>(device); }
};

#endif /* _INCLUDE__USB_SESSION__CLIENT_H_ */
