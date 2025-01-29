/*
 * \brief  Low-level page-fault handling
 * \author Norman Feske
 * \date   2009-10-02
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* Genode includes */
#include <base/cache.h>
#include <base/ipc.h>
#include <base/stdint.h>

/* core includes */
#include <mapping.h>

/* NOVAe includes */
#include <novae/syscalls.h>

namespace Core { class Ipc_pager; }


namespace Core { enum { PAGE_SIZE_LOG2 = 12 }; }


static inline Novae::Rights nova_map_rights(Core::Mapping const &mapping)
{
	return Novae::Rights(true, mapping.writeable, mapping.executable);
}


static inline Novae::Mem_crd nova_src_crd(Core::Mapping const &mapping)
{
	return Novae::Mem_crd(mapping.src_addr >> Core::PAGE_SIZE_LOG2,
	                      mapping.size_log2 - Core::PAGE_SIZE_LOG2,
	                      nova_map_rights(mapping));
}


static inline Novae::Mem_crd nova_dst_crd(Core::Mapping const &mapping)
{
	return Novae::Mem_crd(mapping.dst_addr >> Core::PAGE_SIZE_LOG2,
	                      mapping.size_log2 - Core::PAGE_SIZE_LOG2,
	                      nova_map_rights(mapping));
}


class Core::Ipc_pager
{
	private:

		addr_t  _pd_dst;
		addr_t  _pd_core;
		addr_t  _fault_ip;
		addr_t  _fault_addr;
		addr_t  _sp;
		uint8_t _fault_type;
		uint8_t _syscall_res;
		uint8_t _normal_ipc;

	public:

		Ipc_pager (Novae::Utcb &, addr_t pd_dst, addr_t pd_core, bool);

		/*
		 * Intel manual: 6.15 EXCEPTION AND INTERRUPT REFERENCE
		 *                    Interrupt 14—Page-Fault Exception (#PF)
		 */
		enum {
			ERR_I = 1 << 4,
			ERR_R = 1 << 3,
			ERR_U = 1 << 2,
			ERR_W = 1 << 1,
			ERR_P = 1 << 0,
		};

		/**
		 * Answer current page fault
		 */
		void reply_and_wait_for_fault();

		/**
		 * Request instruction pointer of current fault
		 */
		addr_t fault_ip() { return _fault_ip; }

		/**
		 * Request page-fault address of current fault
		 */
		addr_t fault_addr() { return _fault_addr; }

		/**
		 * Set page-fault reply parameters
		 */
		void set_reply_mapping(Mapping const &);

		/**
		 * Return true if fault was a write fault
		 */
		bool write_fault() const { return _fault_type & ERR_W; }

		/**
		 * Return true if fault was a non-executable fault
		 */
		bool exec_fault() const {
			return _fault_type & ERR_P && _fault_type & ERR_I; }

		/**
		 * Return result of delegate syscall
		 */
		uint8_t syscall_result() const { return _syscall_res; }

		/**
		 * Return low level fault type info
		 * Intel manual: 6.15 EXCEPTION AND INTERRUPT REFERENCE
		 *                    Interrupt 14—Page-Fault Exception (#PF)
		 */
		addr_t fault_type() { return _fault_type; }

		/**
		 * Return stack pointer address valid during page-fault
		 */
		addr_t sp() { return _sp; }
};
