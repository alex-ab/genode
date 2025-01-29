/*
 * \brief  Signal-source server interface
 * \author Alexander Boettcher
 * \date   2024-11-17
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <base/rpc_server.h>
#include <signal_source/novae_signal_source.h>


namespace Genode { struct Signal_source_rpc_object; }


struct Genode::Signal_source_rpc_object : Rpc_object<Novae_signal_source,
                                                     Signal_source_rpc_object>
{
	protected:

		Native_capability _notify { };

	public:

		Signal_source_rpc_object() { }

		void register_semaphore(Native_capability const &cap)
		{
			_notify = cap;
		}
};
