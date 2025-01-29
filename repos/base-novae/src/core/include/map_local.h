/*
 * \brief  Core-local mapping
 * \author Alexander Boettcher
 * \date   2024-06-19
 */

/*
 * Copyright (C) 2024-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* Genode includes */
#include <base/thread.h>

/* core includes */
#include <novae_util.h>

namespace Core {

	/**
	 * Map pages locally within core, used by Stack_area_region_map
	 *
	 * \param from_phys  physical source address
	 * \param to_virt    core-local destination address
	 * \param num_pages  number of pages to map
	 *
	 * \return true on success
	 */
	inline bool map_local(addr_t from_phys, addr_t to_virt, size_t num_pages,
	                      bool read = true, bool write = true, bool exec = true)
	{
		return map_phys_to_core(from_phys, to_virt, num_pages,
		                        Novae::Rights(read, write, exec));
	}

	/**
	 * Unmap pages locally within core
	 *
	 * \param virt       core-local address
	 * \param num_pages  number of pages to unmap
	 */
	inline void unmap_local(addr_t virt, size_t num_pages)
	{
		return ::unmap_local(virt, num_pages, { });
	}
}
