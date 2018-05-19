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

#ifndef _CORE__INCLUDE__PLATFORM_PD_H_
#define _CORE__INCLUDE__PLATFORM_PD_H_

#include <base/allocator.h>
#include <platform.h>
#include <platform_thread.h>
#include <address_space.h>
#include <util/bit_array.h>

namespace Genode {

	class Platform_thread;
	class Platform_pd : public Address_space
	{
		private:

			typedef Bit_array<Platform::MAX_SUPPORTED_CPUS> Sg_in_use;

			Native_capability _parent { };
			int               _thread_cnt;
			addr_t const      _pd_sel;
			addr_t const      _sg_sel_base;
			Sg_in_use         _sg_sel_used { };
			const char *      _label;

			/*
			 * Noncopyable
			 */
			Platform_pd(Platform_pd const &);
			Platform_pd &operator = (Platform_pd const &);

		public:

			/**
			 * Constructors
			 */
			Platform_pd(Allocator * md_alloc, char const *,
			            signed pd_id = -1, bool create = true);

			/**
			 * Destructor
			 */
			~Platform_pd();

			/**
			 * Bind thread to protection domain
			 */
			bool bind_thread(Platform_thread *thread);

			/**
			 * Unbind thread from protection domain
			 *
			 * Free the thread's slot and update thread object.
			 */
			void unbind_thread(Platform_thread *thread);

			/**
			 * Assign parent interface to protection domain
			 */
			void assign_parent(Native_capability parent);

			/**
			 * Return portal capability selector for parent interface
			 */
			addr_t parent_pt_sel() { return _parent.local_name(); }

			/**
			 * Capability selector of this task.
			 *
			 * \return PD selector
			 */
			addr_t pd_sel() const { return _pd_sel; }

			/**
			 * Capability scheduling group selector of this task.
			 *
			 * \return SC selector
			 */
			addr_t sg_sel(unsigned cpu) const { return _sg_sel_base + cpu; }
			bool sg_sel_valid(unsigned cpu) const { return _sg_sel_used.get(cpu, 1); }
			void sg_sel_enabled(unsigned cpu) { _sg_sel_used.set(cpu, 1); }

			/**
			 * Label of this protection domain
			 *
			 * \return name of this protection domain
			 */
			const char * name() const { return _label; }

			/*****************************
			 ** Address-space interface **
			 *****************************/

			void flush(addr_t, size_t, Core_local_addr) override;
	};
}

#endif /* _CORE__INCLUDE__PLATFORM_PD_H_ */
