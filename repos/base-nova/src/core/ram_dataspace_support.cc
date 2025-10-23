/*
 * \brief  Export RAM dataspace as shared memory object
 * \author Norman Feske
 * \author Alexander Boettcher
 * \date   2009-10-02
 */

/*
 * Copyright (C) 2009-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/thread.h>

/* core includes */
#include <ram_dataspace_factory.h>
#include <platform.h>
#include <util.h>
#include <nova_util.h>

#include <cpu/clflush.h>

/* NOVA includes */
#include <nova/syscalls.h>

using namespace Core;


static inline void * alloc_region(Dataspace_component &ds, const size_t size)
{
	using Region_allocation = Range_allocator::Allocation;

	/*
	 * Allocate range in core's virtual address space
	 *
	 * Start with trying to use natural alignment. If this does not work,
	 * successively weaken the alignment constraint until we hit the page size.
	 */
	void *virt_addr = 0;
	Align align { .log2 = log2(ds.size(), get_page_size_log2()) };
	for (; align.log2 >= AT_PAGE.log2; align.log2--) {

		platform().region_alloc().alloc_aligned(size, align).with_result(
			[&] (Region_allocation &a) { a.deallocate = false; virt_addr = a.ptr; },
			[&] (Alloc_error) { /* try next iteration */ }
		);
		if (virt_addr)
			return virt_addr;
	}

	error("alloc_region of size ", size, " unexpectedly failed");
	return nullptr;
}


static unsigned cpuid_clflush_size()
{
	unsigned long cpuid = 0x1, ebx = 0;

#ifdef __x86_64__
	asm volatile ("cpuid" : "+a" (cpuid), "=b" (ebx) : : "rdx", "rcx");
#else
	asm volatile ("push %%ebx  \n"
	              "cpuid       \n"
	              "movl %%ebx, %%edx\n"
	              "pop  %%ebx" : "+a" (cpuid), "=d" (ebx) : : "ecx");
#endif
	auto const clflush_size = unsigned((ebx >> 8) & 0xff);

	return clflush_size ? clflush_size * 8 : 1 * 8;
}


static void cache_flush_region(addr_t virt, addr_t virt_size)
{
	unsigned const cache_line_size = cpuid_clflush_size();

	for (unsigned i = 0; i < virt_size / cache_line_size; i++)
		clflush((void *)(virt + cache_line_size * i));
}


static bool with_local_mapping(Dataspace_component            &ds,
                               Dataspace_component::Key const  key,
                               auto                     const &fn)
{
	Nova::Utcb &utcb = *reinterpret_cast<Nova::Utcb *>(Thread::myself()->utcb());

	size_t const page_rounded_size = align_addr(ds.size(), AT_PAGE);
	const Nova::Rights rights_rw(true, true, false);

	addr_t local_region = ds.core_local_addr();

	if (!local_region) {
		void * const virt_ptr = alloc_region(ds, page_rounded_size);
		if (!virt_ptr)
			return false;

		if (map_local(platform_specific().core_pd_sel(), utcb, ds.phys_addr(),
		              reinterpret_cast<addr_t>(virt_ptr),
		              page_rounded_size >> get_page_size_log2(), rights_rw,
		              key.id, true)) {
			platform().region_alloc().free(virt_ptr, page_rounded_size);
			return false;
		}

		local_region = addr_t(virt_ptr);
	}

	bool free_region = fn(local_region, page_rounded_size);

	if (!free_region)
		return true;

	unmap_local(utcb, local_region, page_rounded_size >> get_page_size_log2());

	platform().region_alloc().free((void *)free_region, page_rounded_size);

	return true;
}


void Ram_dataspace_factory::_revoke_ram_ds(Dataspace_component &ds)
{
	if (!ds.key().id)
		return;

	bool ok = with_local_mapping(ds, ds.key(), [&](addr_t virt, addr_t const virt_size) {
		cache_flush_region(virt, virt_size);
		return true; /* free virtual region */
	});

	if (!ok)
		warning(__func__, " failed");
}


void Ram_dataspace_factory::_clear_ds(Dataspace_component &ds)
{
	bool ok = with_local_mapping(ds, ds.key(), [&](addr_t virt, addr_t const virt_size) {
		auto memset_count = virt_size / 4;

		if ((memset_count * 4 == virt_size) && !(virt & 0x3))
			asm volatile ("rep stosl" : "+D" (virt), "+c" (memset_count)
			                          : "a" (0)  : "memory");
		else
			memset(reinterpret_cast<void *>(virt), 0, virt_size);

		/* we don't keep any core-local mapping */
		ds.assign_core_local_addr(nullptr);

		return true; /* free virtual region */
	});

	if (!ok)
		warning(__func__, " failed");
}


bool Ram_dataspace_factory::_export_ram_ds(Dataspace_component &ds)
{
	if (ds.key().id && ds.key().id >= kernel_hip().tme_kmax)
		return false;

	/* with_local_mapping expect it to be zero in the beginning */
	ds.assign_core_local_addr(nullptr);

	/*
	 * Flush caches with default encryption key, in case the region was in use
	 * - either during bootstrap of the system or
	 * - by core, e.g due to local meta data usage and meta data release
	 */
	if (ds.key().id) {
		Dataspace_component::Key key_id { CORE_ENC_KEY_ID };

		with_local_mapping(ds, key_id, [&](addr_t virt, addr_t virt_size) {
			cache_flush_region(virt, virt_size);
			return true; /* free virtual region */
		});
	}

	return with_local_mapping(ds, ds.key(), [&](addr_t const virt, addr_t) {
		/* assign virtual address to the dataspace to be used by clear_ds */
		ds.assign_core_local_addr((void *)virt);
		return false; /* keep virtual region, stored in ds, for _clear_ds */
	});
}
