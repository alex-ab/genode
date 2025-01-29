/*
 * \brief  RPC capability factory
 * \author Norman Feske
 * \date   2016-01-19
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <rpc_cap_factory.h>
#include <platform.h>
#include <pd_session_component.h>

/* NOVAe includes */
#include <novae_util.h>
#include <novae/capability_space.h>

using namespace Core;


Native_capability Rpc_cap_factory::alloc(Pd_session &pd, Native_capability ep,
                                         addr_t entry, addr_t mtd)
{
	addr_t const pt_sel = cap_map().insert();
	addr_t const pd_sel = platform_specific().core_pd_sel();
	addr_t const ec_sel = ep.local_name();

	using namespace Novae;

	Mutex::Guard guard(_mutex);

	/* create cap object */
	Cap_object * pt_cap = new (&_slab) Cap_object(pt_sel);
	if (!pt_cap)
		return Native_capability();

	_list.insert(pt_cap);

	auto pd_component = dynamic_cast<Pd_session_component *>(&pd);

	if (Pager_object::verbose_rpc_track)
		warning("Rpc_cap_factory::alloc - "
		        "cap=",   Hex(ec_sel), ":", Hex(ep.local_name()), " "
		        "entry=", Hex(entry),  " "
		        "mtd=",   Hex(mtd),    " "
		        "xpt=",   Hex(pt_sel),
		        !pd_component ? " unknown (core?!) PD" : "");

	if (pd_component) {
		pd_component->with_platform_pd([&](auto const & pd) {
			if (Pager_object::verbose_rpc_track)
				warning("Rpc_cap_factory::alloc - "
				        "cap=",   Hex(ec_sel), ":", Hex(ep.local_name()), " "
				        "entry=", Hex(entry),  " "
				        "mtd=",   Hex(mtd),    " "
				        "xpt=",   Hex(pt_sel),
				        "dst_pd_sel=", Hex(pd.pd_sel_obj()));

			Pager_object::track_rpc_cap(pd.pd_sel_obj(), pt_sel);
		});
	}

	/* create portal */
	uint8_t const res = create_pt(pt_sel, pd_sel, ec_sel, entry);
	if (res == NOVA_OK)
		return Capability_space::import(pt_sel);

	error("cap alloc - "
	      "cap=",   Hex(ec_sel), ":", Hex(ep.local_name()), " "
	      "entry=", Hex(entry),  " "
	      "mtd=",   Hex(mtd),    " "
	      "xpt=",   Hex(pt_sel), " "
	      "res=",   res);

	_list.remove(pt_cap);
	destroy(&_slab, pt_cap);

	/* cleanup unused selectors */
	cap_map().remove(pt_sel, 0);

	return Native_capability();
}


void Rpc_cap_factory::free(Native_capability cap)
{
	if (!cap.valid()) return;

	auto const core_pd = platform_specific().core_obj_sel();

	Mutex::Guard guard(_mutex);

	for (auto * obj = _list.first(); obj ; obj = obj->next()) {
		if (cap.local_name() != long(obj->_cap_sel))
			continue;

		Pager_object::untrack_rpc_cap(obj->_cap_sel);

		revoke(core_pd, Novae::Obj_crd(obj->_cap_sel, 0));
		cap_map().remove(obj->_cap_sel, 0);

		_list.remove(obj);
		destroy(&_slab, obj);

		return;
	}

	warning("attempt to free invalid cap object");
}


Rpc_cap_factory::Rpc_cap_factory(Allocator &md_alloc)
: _slab(md_alloc, _initial_sb) { }


Rpc_cap_factory::~Rpc_cap_factory()
{
	auto const core_pd = platform_specific().core_obj_sel();

	Mutex::Guard guard(_mutex);

	for (Cap_object *obj; (obj = _list.first()); ) {

		Pager_object::untrack_rpc_cap(obj->_cap_sel);

		revoke(core_pd, Novae::Obj_crd(obj->_cap_sel, 0));
		cap_map().remove(obj->_cap_sel, 0);

		_list.remove(obj);
		destroy(&_slab, obj);
	}
}
