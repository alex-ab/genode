/*
 * \brief  NOVA-specific convenience functions
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \author Alexander Boettcher
 * \date   2010-01-19
 */

/*
 * Copyright (C) 2010-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* NOVAe includes */
#include <novae/syscalls.h>

/* core includes */
#include <types.h>
#include <util.h>


inline Novae::uint8_t async_map(Genode::addr_t const source_pd,
                                Genode::addr_t const target_pd,
                                Novae::Crd const source,
                                Novae::Crd const target,
                                unsigned   const mad = Novae::Cacheability::write_back())
{
	return Novae::pd_ctrl(source_pd, target_pd,
	                      source.addr() | source.order(),
	                      target.addr() | target.rights(),
	                      mad);
}


inline Novae::uint8_t modify(Genode::addr_t const pd,
                             Novae::Crd     const target)
{
	return Novae::pd_ctrl(pd, pd,
	                      target.addr() | target.order(),
	                      target.addr() | target.rights(),
	                      0);
}


inline Novae::uint8_t revoke(Genode::addr_t const pd, Novae::Crd const target)
{
	return Novae::pd_ctrl(pd, pd,
	                      target.addr() | target.order(),
	                      target.addr() | 0 /* no rights */,
	                      0);
}


/**
 * Find least significant set bit in value
 */
inline unsigned char
lsb_bit(unsigned long const &value, unsigned char const shift = 0)
{
	unsigned long const scan  = value >> shift;
	if (scan == 0) return 0;

	unsigned char pos = (unsigned char)__builtin_ctzl(scan);
	unsigned char res = shift ? pos + shift : pos;
	return res;
}


inline int map_local(Genode::addr_t const  pd_src,
                     Genode::addr_t const  pd_dst,
                     Genode::addr_t const  from_start,
                     Genode::addr_t const  to_start,
                     Genode::size_t const  num_pages,
                     Novae::Rights  const &permission)
{
	using namespace Novae;
	using namespace Genode;

	size_t const size = num_pages << get_page_size_log2();

	addr_t const from_end = from_start + size;
	addr_t const to_end   = to_start   + size;

	for (addr_t offset = 0; offset < size; ) {

		addr_t const from_curr = from_start + offset;
		addr_t const to_curr   = to_start   + offset;

		/*
		 * The common alignment corresponds to the number of least significant
		 * zero bits in both addresses.
		 */
		addr_t const common_bits = from_curr | to_curr;

		/* find least set bit in common bits */
		size_t order = lsb_bit(common_bits, get_page_size_log2());

		/* look if flexpage fits into both 'from' and 'to' address range */
		if ((from_end - from_curr) < (1UL << order))
			order = log2(from_end - from_curr);

		if ((to_end - to_curr) < (1UL << order))
			order = log2(to_end - to_curr);

		if (order >= sizeof(void *)*8)
			return 1;

		auto res = async_map(pd_src, pd_dst,
		                     Mem_crd((from_curr >> 12), order - get_page_size_log2(), permission),
		                     Mem_crd((to_curr   >> 12), order - get_page_size_log2(), permission));
		if (res != Novae::NOVA_OK)
			return res;


		/* advance offset by current flexpage size */
		offset += (1UL << order);
	}

	return Novae::NOVA_OK;
}


/**
 * Unmap pages from the local address space
 *
 * \param start       local virtual address
 * \param num_pages   number of pages to unmap
 * \param self        unmap from this pd or solely from other pds
 * \param rights      rights to be revoked, default: all rwx
 */
inline void unmap_local(Genode::addr_t const pd,
                        Genode::addr_t const start,
                        Genode::size_t       num_pages,
                        Novae::Rights  const rwx)
{
	using namespace Novae;
	using namespace Genode;

	Genode::addr_t base = start >> get_page_size_log2();

	if (start & (get_page_size() - 1)) {
		error("unmap failed - unaligned address specified ", Hex(start));
		return;
	}

	while (num_pages) {
		unsigned char const base_bit  = lsb_bit(base);
		unsigned char const order_bit = (unsigned char)min(log2(num_pages), 31U);
		unsigned char const order     = min(order_bit, base_bit);

		Mem_crd const mem_crd(base, order, rwx);
		Crd     const crd = mem_crd;

		Novae::pd_ctrl(pd, pd,
		               crd.addr() | crd.order(),
		               crd.addr() | crd.rights(),
		               0);

		num_pages -= 1UL << order;
		base      += 1UL << order;
	}
}


inline Novae::uint8_t syscall_retry(Core::Pager_object &, auto const &fn)
{
	Novae::uint8_t res;

	res = fn();

	return res;
}


inline Novae::uint8_t map_vcpu_portals(Core::Pager_object &pager,
                                      Genode::addr_t const source_exc_base,
                                      Genode::addr_t const target_exc_base,
                                      Novae::Utcb &,
                                      Genode::addr_t const source_pd)
{
	using Novae::Obj_crd;
	using Novae::NUM_INITIAL_VCPU_PT_LOG2;

	Obj_crd const source_initial_caps(source_exc_base, NUM_INITIAL_VCPU_PT_LOG2);
	Obj_crd const target_initial_caps(target_exc_base, NUM_INITIAL_VCPU_PT_LOG2);

	return async_map(source_pd, pager.pd_sel(),
	                 source_initial_caps, target_initial_caps);
}

inline Novae::uint8_t map_pagefault_portal(Genode::addr_t const source_exc_base,
                                           Genode::addr_t const target_exc_base,
                                           Genode::addr_t const source_pd,
                                           Genode::addr_t const target_pd)
{
	using Novae::Obj_crd;
	using Novae::PT_SEL_PAGE_FAULT;

	Obj_crd const source_initial_caps(source_exc_base + PT_SEL_PAGE_FAULT, 0);
	Obj_crd const target_initial_caps(target_exc_base + PT_SEL_PAGE_FAULT, 0);

	return async_map(source_pd, target_pd,
	                 source_initial_caps, target_initial_caps);
}


inline Novae::Hip const &kernel_hip()
{
	/**
	 * Initial value of esp register, saved by the crt0 startup code.
	 * This value contains the address of the hypervisor information page.
	 */
	extern Genode::addr_t __initial_sp;
	return *reinterpret_cast<Novae::Hip const *>(__initial_sp);
}


inline bool map_phys_to_core(Genode::addr_t const  phys_addr,
                             Genode::addr_t const  virt_addr,
                             Genode::size_t const  num_pages,
                             Novae ::Rights const &permission)
{
	auto res = map_local(Core::Platform::kernel_host_sel(),
	                     Core::Platform::core_host_sel(),
	                     phys_addr, virt_addr, num_pages, permission);

	return res == Novae::NOVA_OK;
}


inline void unmap_local(Genode::addr_t const start,
                        Genode::size_t const num_pages,
                        Novae::Rights  const rwx)
{
	unmap_local(Core::Platform::core_host_sel(), start, num_pages, rwx);
}
