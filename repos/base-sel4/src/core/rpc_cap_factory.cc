/*
 * \brief  seL4-specific RPC capability factory
 * \author Norman Feske
 * \date   2016-01-19
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/capability.h>
#include <util/misc_math.h>

/* core includes */
#include <rpc_cap_factory.h>
#include <platform.h>

/* base-internal include */
#include <core_capability_space.h>

using namespace Core;


Rpc_cap_factory::Alloc_result Rpc_cap_factory::alloc(Native_capability ep)
{
	if (!ep.valid())
		return Native_capability();

	Mutex::Guard guard(_mutex);

	/*
	 * Pager code uses create_rpc_obj_cap with a cap_sel as rpc obj key. To
	 * avoid collision during destruction, use the same allocator.
	 */
	auto cap_sel = platform_specific().core_sel_alloc().alloc();

	auto res = cap_sel.convert<Rpc_cap_factory::Alloc_result>([&](auto const result) {
		auto rpc_obj_key = Rpc_obj_key(result);

		auto cap = Capability_space::create_rpc_obj_cap(ep, rpc_obj_key);

		try {
			if (cap.valid()) {
				_pool.insert(new (_entry_slab) Entry(cap));
				return Alloc_result(cap);
			} else
				error("no valid cap");
		}
		catch (Out_of_caps) { return Alloc_result(Alloc_error::OUT_OF_CAPS); }
		catch (Out_of_ram)  { return Alloc_result(Alloc_error::OUT_OF_RAM);  }
		catch (Denied)      { error("denied A"); return Alloc_result(Alloc_error::DENIED);      }

		error("denied B");

		return Alloc_result( Alloc_error::DENIED );
	}, [](auto) {
		error("denied C");
		return Alloc_result(Alloc_error::DENIED);
	});

	if (!res.ok() && cap_sel.ok()) {
		error("free cap_sel due to !ok");
		cap_sel.with_result([&](auto const result) {
			platform_specific().core_sel_alloc().free(Cap_sel(unsigned(result)));
		}, [](auto) { /* cap_sel is ok() == true */ });
	}

	return res;
}


void Rpc_cap_factory::free(Native_capability cap)
{
	if (!cap.valid())
		return;

	Mutex::Guard guard(_mutex);

	Entry * entry_ptr = nullptr;
	_pool.apply(cap, [&] (Entry * ptr) {
		if (!ptr)
			return;

		_pool.remove(ptr);

		/* revert Rpc_cap_factory::alloc Cap_sel allocation */
		if (cap.data()) {
			Cap_sel sel (unsigned(Capability_space::rpc_obj_key(*cap.data()).value()));
			platform_specific().core_sel_alloc().free(sel);
		}

		Capability_space::destroy_rpc_obj_cap(cap);

		entry_ptr = ptr;
	});

	if (entry_ptr)
		destroy(_entry_slab, entry_ptr);
}


Rpc_cap_factory::~Rpc_cap_factory()
{
	Mutex::Guard guard(_mutex);

	_pool.remove_all([this] (Entry *ptr) {
		if (ptr)
			destroy(_entry_slab, ptr); });
}
