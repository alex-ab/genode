/*
 * \brief  Client-side interface for ARM device
 * \author Stefan Kalkowski
 * \author Norman Feske
 * \date   2020-04-15
 */

/*
 * Copyright (C) 2020-2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__SPEC__ARM__PLATFORM_DEVICE__CLIENT_H_
#define _INCLUDE__SPEC__ARM__PLATFORM_DEVICE__CLIENT_H_

#include <platform_session/platform_session.h>
#include <platform_device/platform_device.h>
#include <util/mmio.h>
#include <io_mem_session/client.h>

namespace Platform { struct Device_client; }


struct Platform::Device_client : Rpc_client<Device>
{
	Device_client(Device_capability device) : Rpc_client<Device>(device) { }

	Irq_session_capability irq(unsigned id = 0) override
	{
		return call<Rpc_irq>(id);
	}

	Io_mem_session_capability
	io_mem(unsigned id, Range &range, Cache cache) override
	{
		return call<Rpc_io_mem>(id, range, cache);
	}


	/***************************
	 ** Convenience utilities **
	 ***************************/

	Io_mem_dataspace_capability
	io_mem_dataspace(unsigned id = 0, Cache cache = Cache::UNCACHED)
	{
		Range range { };
		Io_mem_session_client session(io_mem(id, range, cache));
		return session.dataspace();
	}
};


class Platform::Device::Mmio : Range, Attached_dataspace, public Genode::Mmio
{
	private:

		Dataspace_capability _dataspace_cap(Device &device, unsigned id)
		{
			Io_mem_session_client io_mem(device.io_mem(id, *this, UNCACHED));
			return io_mem.dataspace();
		}

	public:

		Mmio(Region_map &local_rm, Device &device, unsigned id)
		:
			Attached_dataspace(local_rm, _dataspace_cap(device, id)),
			Genode::Mmio((addr_t)(local_addr<char>() + Range::start))
		{ }
};

#endif /* _INCLUDE__SPEC__ARM__PLATFORM_DEVICE__CLIENT_H_ */
