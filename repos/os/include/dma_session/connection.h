/*
 * \brief  Connection to DMA service
 * \author Stefan Kalkowski
 * \date   2026-05-05
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__DMA_SESSION__CONNECTION_H_
#define _INCLUDE__DMA_SESSION__CONNECTION_H_

#include <base/connection.h>
#include <base/env.h>
#include <base/rpc_client.h>
#include <dma_session/dma_session.h>

namespace Dma {
	struct Buffer;
	struct Connection;
}


class Dma::Connection : public Genode::Connection<Session>,
                        public Genode::Rpc_client<Session>
{
	private:

		/* 'Buffer' accesses the '_env' member */
		friend class Buffer;

		Env &_env;

	public:

		Connection(Env &env, Label label = Label())
		:
			Genode::Connection<Session>(env, label, Ram_quota { RAM_QUOTA }, Args()),
			Rpc_client<Session>(cap()),
			_env(env)
		{ }

		Alloc_result alloc(size_t size, Cache cache) override
		{
			return retry(Ram_quota{max((size_t)4096, size)}, Cap_quota{2}, [&] () {
				return call<Rpc_alloc>(size, cache); });
		}

		void free(Ram_dataspace_capability cap) override {
			call<Rpc_free>(cap); }

		addr_t bus_addr(Ram_dataspace_capability cap) override
		{
			addr_t const result = call<Rpc_bus_addr>(cap);

			/* the platform driver may lack the 'managing_system' role */
			if (!result)
				warning("unable to obtain DMA address from platform driver");

			return result;
		}
};

#endif /* _INCLUDE__PLATFORM_SESSION__CONNECTION_H_ */
