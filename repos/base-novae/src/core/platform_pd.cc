/*
 * \brief  Protection-domain facility
 * \author Norman Feske
 * \date   2009-10-02
 */

/*
 * Copyright (C) 2009-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <util/flex_iterator.h>

/* core includes */
#include <platform.h>
#include <platform_pd.h>

#include <novae_util.h>

using namespace Core;


void Platform_pd::assign_parent(Native_capability parent)
{
	if (!_parent.valid() && parent.valid())
		_parent = parent;
}


Platform_pd::Platform_pd(Accounted_mapped_ram_allocator &,
                         Allocator &, Name const &name)
:
	_pd_base(cap_map().insert(2)), name(name)
{
	if (_pd_base == Native_thread::INVALID_INDEX) {
		error("platform pd creation failed ");
		return;
	}

	using namespace Novae;

	auto const core_pd = platform_specific().core_pd_sel();

	/* create PD */

	auto                res = create_pd(pd_sel()     ,  core_pd, 0 /* protection domain */);
	if (res == NOVA_OK) res = create_pd(pd_sel_obj() , pd_sel(), 1 /* object space */);
	if (res == NOVA_OK) res = create_pd(pd_sel_host(), pd_sel(), 2 /* host space */);
#ifdef __x86_64__
	if (res == NOVA_OK) res = create_pd(_pd_base + 3,  pd_sel(), 5 /* pio  space */);
#endif

	if (res == NOVA_OK) {
		auto const src_obj = platform_specific().core_obj_sel();
		auto const dst_obj = pd_sel_obj();
		auto const rights  = Obj_crd::RIGHT_OBJ_GRANT | Obj_crd::RIGHT_OBJ_TAKE;

		Obj_crd src_caps(pd_sel_obj()  , 0, rights);
		Obj_crd dst_caps(OBJ_SEL_REVOKE, 0, rights);

		res = async_map(src_obj, dst_obj, src_caps, dst_caps);
	}

	if (res != NOVA_OK)
		error("platform pd creation failed ", res);
}


Platform_pd::~Platform_pd()
{
	if (_pd_base == Native_thread::INVALID_INDEX)
		return;

	Pager_object::wipe_all_caps(pd_sel_obj());

	/* Revoke and free cap, pd is gone */
	revoke(platform_specific().core_obj_sel(), Novae::Obj_crd(_pd_base, 2));
	cap_map().remove(_pd_base, 2);
}


void Platform_pd::flush(addr_t remote_virt, size_t size, Core_local_addr)
{
	Flexpage_iterator flex(remote_virt, size, remote_virt, size, 0);
	Flexpage page = flex.page();

	if (pd_sel() == Native_thread::INVALID_INDEX)
		return;

	while (page.valid()) {
		Novae::Mem_crd mem(page.addr >> 12, page.log2_order - 12,
		                   Novae::Rights::none());
		revoke(pd_sel_host(), mem);

		page = flex.page();
	}
}
