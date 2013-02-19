#include <base/env.h>
#include "mem.h"

Genode::Ram_dataspace_capability Genode::Mem::alloc_dma_buffer(size_t size) {
	return Genode::env()->ram_session()->alloc(size, false); }
