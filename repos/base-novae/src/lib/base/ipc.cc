/*
 * \brief  Implementation of the IPC API for NOVAe
 * \author Norman Feske
 * \date   2009-10-02
 */

/*
 * Copyright (C) 2009-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/ipc.h>
#include <base/thread.h>
#include <base/log.h>

/* base-internal includes */
#include <base/internal/ipc.h>

/* NOVAe includes */
#include <novae/cap_map.h>

using namespace Genode;


/****************
 ** IPC client **
 ****************/

Rpc_exception_code Genode::ipc_call(Native_capability const   dst,
                                    Msgbuf_base             & snd_msg,
                                    Msgbuf_base             & rcv_msg,
                                    size_t            const   rcv_cap_count)
{
	static uint64_t local_ipc_id;

	Thread * const myself = Thread::myself();
	Novae::Utcb   &utcb   = *(Novae::Utcb *)myself->utcb();

	auto pt_sel_delegate = Novae::PT_SEL_DELEGATE
	                     + (myself ? myself->native_thread().exc_pt_sel : 0);

	if (!local_ipc_id) {
		local_ipc_id = init_transaction_id(utcb, pt_sel_delegate);
	}

	uint64_t transaction_id = rcv_cap_count || snd_msg.used_caps()
	                        ? __atomic_add_fetch (&local_ipc_id, 2, __ATOMIC_SEQ_CST)
	                        : 0;

	if (rcv_cap_count)
		rpc_id_register(utcb, pt_sel_delegate, transaction_id, dst.local_name());

	unsigned msg_items = copy_msgbuf_to_utcb(dst.local_name(), transaction_id,
	                                         pt_sel_delegate,
	                                         utcb, snd_msg, transaction_id);
	if (!msg_items)
		return Rpc_exception_code(Rpc_exception_code::INVALID_OBJECT);

	unsigned mtd = msg_items - 1;
	uint8_t  res = Novae::call(dst.local_name(), mtd);

	if (res != Novae::NOVA_OK)
		return Rpc_exception_code(Rpc_exception_code::INVALID_OBJECT);

	auto code = copy_utcb_to_msgbuf(transaction_id + 1, utcb,
	                                rcv_msg, mtd + 1, rcv_cap_count);

	/* XXX be nice to core atm, so that core can remove stale transaction id */
	if (rcv_cap_count != rcv_msg.used_caps())
		rpc_id_cancel(utcb, pt_sel_delegate, transaction_id + 1);

	return Rpc_exception_code(int(code));
}
