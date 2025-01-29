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


Platform_pd::Platform_pd(Allocator &, char const *label, signed, bool)
:
	_pd_base(cap_map().insert(2)), _label(label)
{
	if (_pd_base == Native_thread::INVALID_INDEX) {
		error("platform pd creation failed ");
		return;
	}

	/* create PD */
	auto res = Novae::create_pd(_pd_base, platform_specific().core_pd_sel(),
	                            0 /* protection domain */);
	if (res == Novae::NOVA_OK)
		res = Novae::create_pd(_pd_base + 1, _pd_base, 1 /* object space */);
	if (res == Novae::NOVA_OK)
		res = Novae::create_pd(_pd_base + 2, _pd_base, 2 /* host space */);
	if (res == Novae::NOVA_OK)
		res = Novae::create_pd(_pd_base + 3, _pd_base, 5 /* pio  space */);

	if (res != Novae::NOVA_OK)
		error("platform pd creation failed - create_pd ", res);
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
