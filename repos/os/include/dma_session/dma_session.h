/*
 * \brief  DMA session interface
 * \author Stefan Kalkowski
 * \date   2026-05-05
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__DMA_SESSION__DMA_SESSION_H_
#define _INCLUDE__DMA_SESSION__DMA_SESSION_H_

#include <base/rpc_args.h>
#include <base/cache.h>
#include <base/ram_allocator.h>
#include <session/session.h>
#include <util/attempt.h>

namespace Dma {

	using namespace Genode;

	struct Session;
}


struct Dma::Session : Genode::Session
{
	/**
	 * \noapi
	 */
	static const char *service_name() { return "Dma"; }

	static constexpr unsigned RAM_QUOTA = 84*1024;
	static constexpr unsigned CAP_QUOTA = 18;

	virtual ~Session() { }

	using Alloc_result = Attempt<Ram_dataspace_capability, Alloc_error>;

	/**
	  * Allocate memory suitable for DMA
	  */
	virtual Alloc_result alloc(size_t, Cache) = 0;

	/**
	 * Free previously allocated DMA memory
	 */
	virtual void free(Ram_dataspace_capability) = 0;

	/**
	 * Return the bus address of the previously allocated DMA memory
	 */
	virtual addr_t bus_addr(Ram_dataspace_capability) = 0;


	/*********************
	 ** RPC declaration **
	 *********************/

	GENODE_RPC(Rpc_alloc, Alloc_result, alloc, size_t, Cache);
	GENODE_RPC(Rpc_free, void, free, Ram_dataspace_capability);
	GENODE_RPC(Rpc_bus_addr, addr_t, bus_addr, Ram_dataspace_capability);

	GENODE_RPC_INTERFACE(Rpc_alloc, Rpc_free, Rpc_bus_addr);
};

#endif /* _INCLUDE__DMA_SESSION__DMA_SESSION_H_ */

