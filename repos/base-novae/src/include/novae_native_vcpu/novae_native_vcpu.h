 /*
 * \brief  NOVAe vCPU RPC interface
 * \author Christian Helmuth
 * \author Alexander Böttcher
 * \date   2021-01-19
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <vm_session/vm_session.h>
#include <dataspace/dataspace.h>

struct Genode::Vm_session::Native_vcpu : Interface
{
	GENODE_RPC(Rpc_startup, void, startup);
	GENODE_RPC(Rpc_exit_handler, void, exit_handler, unsigned, Signal_context_capability);

	GENODE_RPC_INTERFACE(Rpc_startup, Rpc_exit_handler);
};
