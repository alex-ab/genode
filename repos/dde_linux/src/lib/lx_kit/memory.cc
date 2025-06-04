/*
 * \brief  Lx_kit memory allocation backend
 * \author Stefan Kalkowski
 * \author Christian Helmuth
 * \date   2021-03-25
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

/* Genode includes */
#include <base/log.h>

/* local includes */
#include <lx_kit/memory.h>
#include <lx_kit/map.h>
#include <lx_kit/byte_range.h>


void Lx_kit::Mem_allocator::free_buffer(void * addr)
{
	Buffer * buffer = nullptr;

	_virt_to_dma.apply(Buffer_info::Query_addr(addr),
	                   [&] (Buffer_info const & info) {
		buffer = &info.buffer;
	});

	if (!buffer) {
		warning(__func__, ": no memory buffer for addr: ", addr, " found");
		return;
	}

	void const * virt_addr = (void const *)buffer->virt_addr();
	void const * dma_addr  = (void const *)buffer->dma_addr();

	_virt_to_dma.remove(Buffer_info::Query_addr(virt_addr));
	_dma_to_virt.remove(Buffer_info::Query_addr(dma_addr));

	destroy(_heap, buffer);
}


Genode::Dataspace_capability Lx_kit::Mem_allocator::attached_dataspace_cap(void * addr)
{
	Genode::Dataspace_capability ret { };

	_virt_to_dma.apply(Buffer_info::Query_addr(addr),
	                   [&] (Buffer_info const & info) {
		ret = info.buffer.cap();
	});

	return ret;
}


void * Lx_kit::Mem_allocator::alloc(size_t const size, size_t const align,
                                    void (*new_range_cb)(void const *, unsigned long))
{
	if (!size)
		return nullptr;

	auto cleared_allocation = [] (void * const ptr, size_t const size) {
		memset(ptr, 0, size);
		return ptr;
	};

	auto log2_align = !align ? 0 : unsigned(log2(align));
	/* adjust log2_align in case that not only one bit is set in align */
	if ((1ul << log2_align) < align)
		log2_align += 1;

	return _mem.alloc_aligned(size, log2_align).convert<void *>(

		[&] (void *ptr) { return cleared_allocation(ptr, size); },

		[&] (Range_allocator::Alloc_error) {

			/*
			 * Restrict the minimum buffer size to avoid the creation of
			 * a separate dataspaces for tiny allocations.
			 */
			size_t const min_buffer_size = 128*1024;

			/*
			 * Allocate at least one more byte that is not officially
			 * registered at the '_mem' ranges.
			 * This way, two virtual consecutive ranges
			 * (that must be assumed to belong to non-contiguous physical
			 * ranges) can never be merged when freeing an allocation. Such
			 * a merge would violate the assumption that a both the virtual
			 * and physical addresses of a multi-page allocation are always
			 * contiguous.
			 */

			/*
			 * buggy: Buffer & buffer = alloc_buffer(max(size + 1, min_buffer_size));
			 *
			 * Using buf_size + 1 lead to dataspace allocations, which have
			 * more than one bit set in the size,
			 * e.g. log2_align=16, size=0x10000 + 1 -> 0x10001
			 *      -> rounded up by core to ds_size=0x11000.
			 *
			 * Such a dataspace is not attached by core ever at log2_align
			 * addresses. However, the following code below assumes so and
			 * fails in such cases with "memory allocation failed for "...
			 *
			 * As stop gap solution, the code now uses the next larger aligned
			 * data space size, which core *in most cases* will attach aligned.
			 * Downside is, that allocations are now larger.
			 *
			 * This is a stop gap solution, better fix pending, see #5412
			 * issue.
			 */
			auto buf_size = max(1ul << log2_align, max(size, min_buffer_size));

			if (buf_size <= max(size, min_buffer_size)) {
				/* doubling assures that next log2_align allocation will fit */
				buf_size = 2 * max(1ul << log2_align, buf_size);
			}

			Buffer & buffer = alloc_buffer(buf_size);

			_mem.add_range(buffer.virt_addr(), buffer.size() - 1);

			/* re-try allocation */
			void * const virt_addr = _mem.alloc_aligned(size, log2_align).convert<void *>(

				[&] (void *ptr) { return cleared_allocation(ptr, size); },

				[&] (Range_allocator::Alloc_error) -> void * {
					error("memory allocation failed for ", size, " align ", align);
					return nullptr; }
			);

			if (virt_addr)
				new_range_cb((void *)buffer.virt_addr(), buffer.size() - 1);

			return virt_addr;
		}
	);
}


Genode::addr_t Lx_kit::Mem_allocator::dma_addr(void * addr)
{
	addr_t ret = 0UL;

	_virt_to_dma.apply(Buffer_info::Query_addr(addr),
	                   [&] (Buffer_info const & info) {
		addr_t const offset = (addr_t)addr - info.buffer.virt_addr();
		ret = info.buffer.dma_addr() + offset;
	});

	return ret;
}


Genode::addr_t Lx_kit::Mem_allocator::virt_addr(void * dma_addr)
{
	addr_t ret = 0UL;

	_dma_to_virt.apply(Buffer_info::Query_addr(dma_addr),
	                   [&] (Buffer_info const & info) {
		addr_t const offset = (addr_t)dma_addr - info.buffer.dma_addr();
		ret = info.buffer.virt_addr() + offset;
	});

	return ret;
}


Genode::addr_t Lx_kit::Mem_allocator::virt_region_start(void * virt_addr)
{
	addr_t ret = 0UL;

	_virt_to_dma.apply(Buffer_info::Query_addr(virt_addr),
	                   [&] (Buffer_info const & info) {
		ret = info.buffer.virt_addr();
	});

	return ret;
}


bool Lx_kit::Mem_allocator::free(const void * ptr)
{
	if (!_mem.valid_addr((addr_t)ptr))
		return false;

	using Size_at_error = Allocator_avl::Size_at_error;

	_mem.size_at(ptr).with_result(
		[&] (size_t)        { _mem.free(const_cast<void*>(ptr)); },
		[ ] (Size_at_error) {                                    });

	return true;
}


Genode::size_t Lx_kit::Mem_allocator::size(const void * ptr)
{
	if (!ptr) return 0;

	using Size_at_error = Allocator_avl::Size_at_error;

	return _mem.size_at(ptr).convert<size_t>([ ] (size_t s)      { return s;  },
	                                         [ ] (Size_at_error) { return 0U; });
}


Lx_kit::Mem_allocator::Mem_allocator(Genode::Env          & env,
                                     Heap                 & heap,
                                     Platform::Connection & platform,
                                     Cache                  cache_attr)
: _env(env), _heap(heap), _platform(platform), _cache_attr(cache_attr) {}
