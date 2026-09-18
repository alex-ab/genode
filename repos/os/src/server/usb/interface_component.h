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

#ifndef _SRC__SERVER__USB__INTERFACE_COMPONENT_H_
#define _SRC__SERVER__USB__INTERFACE_COMPONENT_H_

/* Genode */
#include <base/registry.h>
#include <usb_session/usb_session.h>

/* local includes */
#include <device.h>
#include <host_session_component.h>
#include <tx.h>

namespace Usb {
	using namespace Genode;

	class Device_component;
	class Interface_component;

	using Request_id_allocator = Bit_allocator<2048>;
}


class Usb::Interface_component
:
	public  Rpc_object<Interface_session, Interface_component>,
	private Registry<Interface_component>::Element
{
	public:

		using Constructed = Dma_alloc_result;

		struct Request : Host_session_component::Request
		{
			Host_session_component::Request_id const device_id;
			uint8_t const iface_idx;
			Interface_component &session;

			Request(Host_session_component &host_session, Type type,
			        Host_session_component::Request_id dev_id,
			        uint8_t iface_idx, Interface_component &iface_session);

			void generate(Generator &) const override;
			void done() override;
		};

	private:

		Env &_env;

		Attempt<addr_t, Request_id_allocator::Error> _id_allocation;

		Device_component &_device_session;

		uint8_t const _iface_idx;

		Signal_context_capability _source_sigh_cap {};

		Usb::Tx<Interface_session, Interface_component> _tx;

		Constructible<Request> _request {};

		enum class State {
			CREATED, ACQUIRED, RELEASING, UNAVAIL
		} _state;

	public:

		/*
		 * Noncopyable
		 */
		Interface_component(Interface_component const &) = delete;
		Interface_component &operator = (Interface_component const &) = delete;

		Interface_component(Env &env, Device_component &dc, Device &device,
		                    uint8_t iface, size_t buffer_size);

		Interface_component(Env &env, Device_component &dc, size_t buffer_size);

		~Interface_component();

		Constructed constructed() const { return _tx.dma_result(); };

		Host_session_component::Request_id id() const
		{
			using Id = Host_session_component::Request_id;
			return _id_allocation.convert<Id>([] (auto v) { return Id(v); },
			                                  [] (auto) { return Id(~0UL); });
		}

		void acquire(Signal_context_capability source_sigh);
		Device_session::Release_result release();


		/*********************************
		 ** Interface_session interface **
		 *********************************/

		Capability<Interface_session::Tx> tx_cap() { return _tx.cap(); }
};

#endif /* _SRC__SERVER__USB__INTERFACE_COMPONENT_H_ */
