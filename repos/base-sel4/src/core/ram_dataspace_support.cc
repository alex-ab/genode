/*
 * \brief  Export and initialize RAM dataspace
 * \author Norman Feske
 * \date   2015-05-01
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <ram_dataspace_factory.h>
#include <platform.h>
#include <map_local.h>
#include <untyped_memory.h>

using namespace Core;


bool Ram_dataspace_factory::_export_ram_ds(Dataspace_component &ds)
{
	size_t const page_rounded_size = (ds.size() + get_page_size() - 1) & get_page_mask();
	size_t const num_pages = page_rounded_size >> get_page_size_log2();

	if (ds.large()) {
		ASSERT(ds.size() % (1ul << 21) == 0);
		auto size = ds.size() / (1ul << 21);
		error(__func__, " size ", num_pages, " ", page_rounded_size);
		if (Untyped_memory::convert_to_large_page_frame(ds.phys_addr(), size))
			return true;
		error("uiui - failed");
	}
	return Untyped_memory::convert_to_page_frames(ds.phys_addr(), num_pages);
}


void Ram_dataspace_factory::_revoke_ram_ds(Dataspace_component &ds)
{
	size_t const page_rounded_size = (ds.size() + get_page_size() - 1) & get_page_mask();

	if (ds.large())
		error(__func__, " unconvert of large dataspace ???");

	Untyped_memory::convert_to_untyped_frames(ds.phys_addr(), page_rounded_size);
}


void Ram_dataspace_factory::_clear_ds (Dataspace_component &ds)
{
	static Mutex protect_region_alloc { };

	size_t const page_rounded_size = (ds.size() + get_page_size() - 1) & get_page_mask();

	/* allocate one page in core's virtual address space */
	auto alloc_one_virt_page = [&] () -> void *
	{
		Mutex::Guard guard(protect_region_alloc);

		if (ds.large()) {
			return platform().region_alloc().alloc_aligned(1 << 21, 21).convert<void *>(
				[&] (Range_allocator::Allocation &a) {
					a.deallocate = false; return a.ptr; },
				[&] (Alloc_error) -> void * {
					ASSERT_NEVER_CALLED;
					return nullptr;
				});
		}

		return platform().region_alloc().try_alloc(get_page_size()).convert<void *>(
			[&] (Range_allocator::Allocation &a) {
				a.deallocate = false; return a.ptr; },
			[&] (Alloc_error) -> void * {
				ASSERT_NEVER_CALLED;
				return nullptr;
			});
	};

	addr_t const virt_addr = addr_t(alloc_one_virt_page());

	if (!virt_addr)
		return;

	if (ds.large()) {
		auto const large_size_log2 = 21;
		auto const large_size      = 1ul << large_size_log2;
		auto const page_count      = 512;

		ASSERT(ds.size() % large_size == 0);

		auto const large_pages = ds.size() / large_size;

		for (unsigned i = 0; i < large_pages; i++) {

			auto &vm_space = platform_specific().core_vm_space();

			Vm_space::Map_attr const attr { .cached         = true,
			                                .write_combined = false,
			                                .writeable      = true,
			                                .executable     = false,
			                                .flush_support  = false,
			                                .large          = ds.large() };

			auto const phys_addr = ds.phys_addr() + i * large_size;

			bool ok = vm_space.map(phys_addr, virt_addr, page_count, attr);

			if (!ok)
				ASSERT(!"could not map 2M inside core");

			/* clear one large page */
			memset((void *)virt_addr, 0, large_size);

			vm_space.unmap_large(virt_addr, page_count);

			if (!vm_space.alloc_page_tables(virt_addr, large_pages))
				error("could not remap page table");

			Mutex::Guard guard(protect_region_alloc);

			/* free core's virtual address space */
			platform().region_alloc().free((void *)virt_addr, large_size);
		}
	} else {
		/* map each page of dataspace one at a time and clear it */
		for (addr_t offset = 0; offset < page_rounded_size; offset += get_page_size())
		{
			addr_t const phys_addr = ds.phys_addr() + offset;
			enum { ONE_PAGE = 1 };

			/* map one physical page to the core-local address */
			if (!map_local(phys_addr, virt_addr, ONE_PAGE)) {
				error("could not map 4k inside core");
				break;
			}

			/* clear one page */
			memset((void *)virt_addr, 0, get_page_size());

			/* unmap cleared page from core */
			unmap_local(virt_addr, ONE_PAGE, nullptr, ds.cacheability() != CACHED);
		}

		Mutex::Guard guard(protect_region_alloc);

		/* free core's virtual address space */
		platform().region_alloc().free((void *)virt_addr, get_page_size());
	}
}
