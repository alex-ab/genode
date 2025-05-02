/*
 * \brief  Environment initialization
 * \author Norman Feske
 * \author Christian Helmuth
 * \date   2006-07-27
 */

/*
 * Copyright (C) 2006-2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/internal/platform.h>

using namespace Genode;


void Genode::init_parent_resource_requests(Genode::Env &env)
{
	using Parent = Expanding_parent_client;
	static_cast<Parent*>(&env.parent())->init_fallback_signal_handling();
}


Platform &Genode::init_platform()
{
	raw(__func__, " ", __LINE__);
	static Genode::Platform platform;

	raw(__func__, " ", __LINE__);
	init_log(platform.parent);
	raw(__func__, " ", __LINE__);
	init_rpc_cap_alloc(platform.parent);
	raw(__func__, " ", __LINE__);
	init_cap_slab(platform.pd, platform.parent);
	raw(__func__, " ", __LINE__);
	init_thread(platform.cpu, platform.local_rm);
	raw(__func__, " ", __LINE__);
	init_thread_start(platform.pd.rpc_cap());
	raw(__func__, " ", __LINE__);
	init_thread_bootstrap(platform.cpu, platform.parent.main_thread_cap());
	raw(__func__, " ", __LINE__);
	init_signal_receiver(platform.pd, platform.parent);
	error(__func__, " ", __LINE__);

	return platform;
}


void Genode::binary_ready_hook_for_platform() { }
