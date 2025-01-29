/*
 * \brief  Low-level page-fault handling
 * \author Norman Feske
 * \date   2010-01-25
 */

/*
 * Copyright (C) 2010-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/thread.h>

/* core includes */
#include <ipc_pager.h>

/* NOVAe includes */
#include <platform.h>
#include <novae_util.h>

using namespace Core;


void Mapping::prepare_map_operation() const { }


Ipc_pager::Ipc_pager(Novae::Utcb &utcb, addr_t pd_dst, addr_t pd_core, bool normal_ipc)
:
	_pd_dst(pd_dst),
	_pd_core(pd_core),
	_fault_ip(utcb.ip()),
	_fault_addr(utcb.pf_addr()),
	_sp(utcb.sp()),
	_fault_type(utcb.pf_type()),
	_syscall_res(Novae::NOVA_OK),
	_normal_ipc(normal_ipc)
{

	/*
	 * When this function is called from the page-fault handler EC, a page
	 * fault already occurred. So we never wait but immediately read the
	 * page-fault information from our UTCB.
	 */
}


void Ipc_pager::set_reply_mapping(Mapping const &mapping)
{
	auto const mad = mapping.write_combined
	               ? Novae::Cacheability::write_combining()
	               : Novae::Cacheability::write_back();

	/* asynchronously map memory */
	_syscall_res = async_map(_pd_core, _pd_dst, nova_src_crd(mapping),
	                         nova_dst_crd(mapping), mad);
}


void Ipc_pager::reply_and_wait_for_fault()
{
	Thread &myself = *Thread::myself();

	/*
	 * If it was a normal IPC and the mapping failed, caller may re-try.
	 * Otherwise nothing left to be delegated - done asynchronously beforehand.
	 */
	auto const mtd = (_normal_ipc && _syscall_res != Novae::NOVA_OK) ? 1 : 0;

	Novae::reply(myself.stack_top(), mtd);
}
