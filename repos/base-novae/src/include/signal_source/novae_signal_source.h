/*
 * \brief  NOVAe-specific signal source RPC interface
 * \author Norman Feske
 * \date   2011-04-12
 */

/*
 * Copyright (C) 2011-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <base/rpc.h>
#include <signal_source/signal_source.h>

namespace Genode { struct Novae_signal_source; }

struct Genode::Novae_signal_source : Signal_source
{
	GENODE_RPC(Rpc_register_semaphore, void, register_semaphore,
	           Native_capability const &);

	GENODE_RPC_INTERFACE_INHERIT(Signal_source, Rpc_register_semaphore);
};
