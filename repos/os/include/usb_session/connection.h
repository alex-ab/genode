/*
 * \brief  Client connection to USB server
 * \author Stefan Kalkowski
 * \author Sebastian Sumpf
 * \date   2014-12-08
 */

/*
 * Copyright (C) 2014-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */
#ifndef _INCLUDE__USB_SESSION__CONNECTION_H_
#define _INCLUDE__USB_SESSION__CONNECTION_H_

#include <base/attached_dataspace.h>
#include <base/connection.h>
#include <rom_session/client.h>
#include <usb_session/client.h>

namespace Usb {
	class Device;
	struct Connection;
}


class Usb::Connection : public Genode::Connection<Session>, public Usb::Client
{
	private:

		friend class Usb::Device;

		Env                              &_env;
		Rom_session_client                _rom     { devices_rom() };
		Constructible<Attached_dataspace> _ds      {};
		Io_signal_handler<Connection>     _handler { _env.ep(), *this,
		                                             &Connection::_handle_io };

		void _try_attach()
		{
			_ds.destruct();
			try { _ds.construct(_env.rm(), _rom.dataspace()); }
			catch (Attached_dataspace::Invalid_dataspace) {
				warning("Invalid devices rom dataspace returned!");}
		}

		void _handle_io() { }

		Device_capability _wait_for_device(auto const &fn)
		{
			for (;;) {
				/* repeatedly check for availability of device */
				Device_capability cap = fn();
				if (cap.valid())
					return cap;

				_env.ep().wait_and_dispatch_one_io_signal();
			}
		}

		Device_capability _acquire_device(Device_name const &name)
		{
			Ram_quota ram_quota(Device_session::TX_BUFFER_SIZE + 4096);
			return retry(ram_quota, Cap_quota{6}, [&] () {
				return Client::acquire_device(name);
			}).convert<Device_capability>(
				[] (auto cap) { return cap; },
				[] (auto) { return Device_capability(); });
		}

		Device_capability _acquire_single_device()
		{
			return _wait_for_device([&] () {
				Ram_quota ram_quota(Device_session::TX_BUFFER_SIZE + 4096);
				return retry(ram_quota, Cap_quota{6}, [&] () {
					return Client::acquire_single_device();
				}).convert<Device_capability>(
					[] (auto cap) { return cap; },
					[] (auto) { return Device_capability(); });
			});
		}

		void _release_device(Device_capability cap)
		{
			for (;;) {
				if (Client::release_device(cap) == Release_result::OK)
					return;
				_env.ep().wait_and_dispatch_one_io_signal();
			}
		}

	public:

		Connection(Genode::Env    &env,
		           Genode::size_t  ram_quota = RAM_QUOTA)
		:
			Genode::Connection<Session>(env, Label(),
			                            Ram_quota { ram_quota }, Args()),
			Client(cap()),
			_env(env)
		{
			_try_attach();

			/*
			 * Initially register dummy handler, to be able to receive signals
			 * if _wait_for_device probes for a valid devices rom, and if release
			 * device is pending
			 */
			_rom.sigh(_handler);
			release_sigh(_handler);
		}

		void update()
		{
			if (_ds.constructed() && _rom.update() == true)
				return;

			_try_attach();
		}

		void sigh(Signal_context_capability sigh) { _rom.sigh(sigh); }

		void with_node(auto const &fn)
		{
			update();
			if (_ds.constructed() && _ds->local_addr<void const>()) {
				Node node(Const_byte_range_ptr(_ds->local_addr<char>(), _ds->size()));
				fn(node);
			}
		}
};

#endif /* _INCLUDE__USB_SESSION__CONNECTION_H_ */
