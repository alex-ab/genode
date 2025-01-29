/*
 * \brief  Client-side NOVA-specific CPU session interface
 * \author Norman Feske
 * \date   2016-04-21
 */

/*
 * Copyright (C) 2016-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <novae_native_cpu/novae_native_cpu.h>
#include <base/rpc_client.h>

namespace Genode { struct Novae_native_cpu_client; }


struct Genode::Novae_native_cpu_client : Rpc_client<Cpu_session::Native_cpu>
{
	explicit Novae_native_cpu_client(Capability<Cpu_session::Native_cpu> cap)
	: Rpc_client<Cpu_session::Native_cpu>(cap) { }

	void thread_type(Thread_capability thread_cap, Thread_type thread_type,
	                 Exception_base exception_base) override {
		call<Rpc_thread_type>(thread_cap, thread_type, exception_base); }
};
