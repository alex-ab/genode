/*
 * \brief  ARM-device interface
 * \author Stefan Kalkowski
 * \date   2020-04-15
 */

/*
 * Copyright (C) 2020-2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__SPEC__ARM__PLATFORM_DEVICE__PLATFORM_DEVICE_H_
#define _INCLUDE__SPEC__ARM__PLATFORM_DEVICE__PLATFORM_DEVICE_H_

#include <base/rpc.h>
#include <base/exception.h>
#include <io_mem_session/capability.h>
#include <irq_session/capability.h>
#include <util/string.h>

namespace Platform {

	using namespace Genode;

	struct Device;
}


struct Platform::Device : Genode::Session
{
	enum { DEVICE_NAME_LEN = 64 };

	using Name = String<DEVICE_NAME_LEN>;

	/**
	 * Byte-offset range of memory-mapped I/O registers within dataspace
	 */
	struct Range { addr_t start; size_t size; };

	virtual ~Device() { }

	/**
	 * Get IRQ session capability
	 */
	virtual Irq_session_capability irq(unsigned id) = 0;

	/**
	 * Get IO mem session capability of specified resource id
	 */
	virtual Io_mem_session_capability io_mem(unsigned id, Range &, Cache) = 0;

	struct Mmio;


	/*********************
	 ** RPC declaration **
	 *********************/

	GENODE_RPC(Rpc_irq, Irq_session_capability, irq, unsigned);
	GENODE_RPC(Rpc_io_mem, Io_mem_session_capability, io_mem,
	           unsigned, Range &, Cache);

	GENODE_RPC_INTERFACE(Rpc_irq, Rpc_io_mem);
};

#endif /* _INCLUDE__SPEC__ARM__PLATFORM_DEVICE__PLATFORM_DEVICE_H_ */
