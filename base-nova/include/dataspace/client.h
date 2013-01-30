/*
 * \brief  Dataspace client interface
 * \author Alexander Boettcher
 * \date   2012-01-30
 */

/*
 * Copyright (C) 2013-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _INCLUDE__DATASPACE__CLIENT_H_
#define _INCLUDE__DATASPACE__CLIENT_H_

#include <dataspace/capability.h>
#include <base/rpc_client.h>

extern "C" bool genode_iommu;

namespace Genode {

	struct Dataspace_client : Rpc_client<Dataspace>
	{
		private:

			void * _virt;
		
		public:

			explicit Dataspace_client(Dataspace_capability ds, void * virt)
			: Rpc_client<Dataspace>(ds), _virt(virt) { }

			size_t size()      { return call<Rpc_size>();      }
			addr_t phys_addr()
			{
				if (genode_iommu) return reinterpret_cast<addr_t>(_virt);
				return call<Rpc_phys_addr>();
			}
			bool   writable()  { return call<Rpc_writable>();  }
	};
}

#endif /* _INCLUDE__DATASPACE__CLIENT_H_ */
