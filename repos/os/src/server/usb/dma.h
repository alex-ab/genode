/*
 * \brief  Usb multiplexing component - DMA
 * \author Stefan Kalkowski
 * \date   2026-08-05
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__SERVER__USB__DMA_H_
#define _SRC__SERVER__USB__DMA_H_

/* Genode includes */
#include <dma_session/connection.h>

namespace Usb {
	class Dma_connection;
	using Dma_alloc_result = Attempt<Ram_dataspace_capability, Alloc_error>;
}

/**
 * We re-implement the Dma connection here to be able to
 * allocate buffers and thereby handle quota errors explicitely,
 * and to deal with resource shortage in construction.
 */
class Usb::Dma_connection
{
	private:

		Ram_quota_guard &_ram_quota_guard;
		Cap_quota_guard &_cap_quota_guard;

		Ram_quota _ram { Dma::Session::RAM_QUOTA };
		Cap_quota _cap { Dma::Session::CAP_QUOTA };

		Constructible<Dma::Connection> _dma { };

	public:

		using Constructed = Attempt<Ok, Alloc_error>;

		Constructed constructed { Alloc_error::DENIED };

		Dma_connection(Env &env, Session_label &label,
		               Ram_quota_guard &ram,
		               Cap_quota_guard &caps)
		:
			_ram_quota_guard(ram), _cap_quota_guard(caps)
		{
			constructed = _ram_quota_guard.reserve(_ram).convert<Constructed>(
				[&] (auto &reserved_ram) {
					return _cap_quota_guard.reserve(_cap).convert<Constructed>(
						[&] (auto &reserved_caps) {
							reserved_caps.deallocate = false;
							reserved_ram.deallocate  = false;
							_dma.construct(env, label);
							return Ok();
						},
						[&] (auto) { return Alloc_error::OUT_OF_CAPS; });
				},
				[&] (auto) { return Alloc_error::OUT_OF_RAM; });
		}

		~Dma_connection()
		{
			if (constructed.ok()) {
				_ram_quota_guard.replenish(_ram);
				_cap_quota_guard.replenish(_cap);
			}
		}


		Dma_alloc_result try_alloc(size_t size)
		{
			if (!_dma.constructed())
				return Alloc_error::DENIED;

			while (true) {
				auto result = _dma->call<Dma::Session::Rpc_alloc>(size, CACHED);
				auto done = result.template convert<bool>(
					[&] (auto) { return true; },
					[&] (auto error) {

						if (error == decltype(error)::OUT_OF_CAPS) {
							auto caps = Cap_quota(2);
							return _cap_quota_guard.reserve(caps).convert<bool>(
								[&] (auto &reserved_caps) {
									_cap.value += caps.value;
									reserved_caps.deallocate = false;
									_dma->upgrade_caps(caps.value);
									return false;
								},
								[] (auto) { return true; });
						}

						if (error == decltype(error)::OUT_OF_RAM) {
							auto ram = Ram_quota(max(size, 4096UL));
							return _ram_quota_guard.reserve(ram).convert<bool>(
								[&] (auto &reserved_ram) {
									_ram.value += ram.value;
									reserved_ram.deallocate = false;
									_dma->upgrade_ram(ram.value);
									return false;
								},
								[] (auto) { return true; });
						}

						return true;
					});

				if (done)
					return result;
			}
		}

		addr_t bus_addr(Ram_dataspace_capability cap)
		{
			return _dma.constructed() ? _dma->bus_addr(cap) : ~0UL;
		}

		void free(Ram_dataspace_capability cap)
		{
			if (_dma.constructed()) _dma->free(cap);
		}
};

#endif /* _SRC__SERVER__USB__DMA_H_ */
