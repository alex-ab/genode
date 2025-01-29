/*
 * \brief  Platform specific services for NOVAe
 * \author Alexander Boettcher
 * \date   2025-03-15
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <platform_services.h>

/*
 * Add ARM v8 specific services 
 */
void Core::platform_add_local_services(Rpc_entrypoint         &,
                                       Sliced_heap            &,
                                       Registry<Service>      &,
                                       Trace::Source_registry &,
                                       Ram_allocator          &,
                                       Local_rm               &,
                                       Range_allocator        &)
{
}
