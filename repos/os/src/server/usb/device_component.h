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

#ifndef _SRC__SERVER__USB__DEVICE_COMPONENT_H_
#define _SRC__SERVER__USB__DEVICE_COMPONENT_H_

/* local includes */
#include <host_session_component.h>
#include <interface_component.h>

namespace Usb {
	class Session_component;
	class Device_component;
}


class Usb::Device_component
:
	public  Rpc_object<Device_session, Device_component>,
	private Registry<Device_component>::Element
{
	public:

		using Constructed = Dma_alloc_result;

		struct Request : Host_session_component::Request
		{
			Device::Name const device_name;
			Device_component &session;

			Request(Host_session_component &host_session, Type type,
			        Device_id::Name name, Device_component &device_session);

			void generate(Generator &) const override;
			void done() override;
		};

	private:

		friend class Interface_component;

		Env &_env;

		Session_component &_session;

		Attempt<addr_t, Request_id_allocator::Error> _id_allocation;

		Device_id _device_id;

		Signal_context_capability _source_sigh_cap {};
		Signal_context_capability _release_sigh_cap {};

		using Session_allocator =
			Memory::Constrained_obj_allocator<Interface_component>;

		Session_allocator _iface_session_alloc;

		Registry<Interface_component> _iface_registry {};

		Constructible<Dma_connection> _dma {};

		static constexpr size_t BUFFER_SIZE = Device_session::TX_BUFFER_SIZE;

		Dma_alloc_result _alloc_dma(Session_label label, size_t);
		Dma_alloc_result _alloc_ram(size_t);

		void _free_dma(Ram_dataspace_capability);
		void _free_ram(Ram_dataspace_capability);

		Usb::Tx<Device_session, Device_component> _tx;

		Constructible<Request> _request {};

		enum class State {
			CREATED, ACQUIRED, RELEASING, DISCONNECTED, UNAVAIL
		} _state;

	public:

		/*
		 * Noncopyable
		 */
		Device_component(Device_component const &) = delete;
		Device_component &operator = (Device_component const &) = delete;

		Device_component(Env &env, Session_component &sc, Device &device);

		Device_component(Env &env, Session_component &sc);

		~Device_component();

		Constructed constructed() const { return _tx.dma_result(); };

		Host_session_component::Request_id id() const
		{
			using Id = Host_session_component::Request_id;
			return _id_allocation.convert<Id>([] (auto v) { return Id(v); },
			                                  [] (auto) { return Id(~0UL); });
		}

		void acquire(Signal_context_capability source_sigh);

		Release_result release();
		void released(Interface_component&);

		bool acquired(Device const &dev) const {
			return dev.id() == _device_id; }


		/******************************
		 ** Device_session interface **
		 ******************************/

		Capability<Device_session::Tx> tx_cap() { return _tx.cap(); }

		Acquisition_result acquire_interface(uint8_t index, size_t buf_size);
		Release_result release_interface(Interface_capability cap);
		void release_sigh(Signal_context_capability);
};

#endif /* _SRC__SERVER__USB__DEVICE_COMPONENT_H_ */
