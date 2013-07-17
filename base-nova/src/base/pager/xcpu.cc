/*
 * \brief  xCPU IPC emulation code
 * \author Alexander Boettcher
 * \date   2013-07-18
 */

/*
 * Copyright (C) 2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* NOVA includes */
#include <nova/syscalls.h>

#include "xcpu.h"

using namespace Genode;

struct Xcpu_ipc::worker Xcpu_ipc::global_worker[Xcpu_ipc::MAX_SUPPORTED_CPUS];

void Xcpu_ipc::handle(Nova::Utcb * utcb, addr_t block_sm, Nova::Obj_crd rcv_wnd)
{
	using namespace Nova;

	unsigned typed_items = utcb->msg_items();
	unsigned untyped     = utcb->msg_words();

	/* last item refers to the portal to be called actually */
	struct Utcb::Item * item = utcb->get_item(typed_items - 1);

	addr_t target_cpu = utcb->msg[untyped - 2];
	Xcpu_thread * worker = 0;

	if (!item || item->is_del() || target_cpu >= MAX_SUPPORTED_CPUS ||
	    !(worker = Xcpu_ipc::worker(target_cpu))) {
		PERR("xCPU IPC failed - error 0x%p:%lu:0x%p", item, target_cpu, worker);
		utcb->set_msg_word(0);
		return;
	}

	Crd crd(item->crd);
	utcb->msg[0]      = utcb->msg[untyped - 1];

	/* copy untyped items/words to utcb of global worker */
	Nova::Utcb * utcb_worker = reinterpret_cast<Nova::Utcb *>(worker->utcb());
	memcpy(utcb_worker->msg, utcb->msg, (untyped - 2) * sizeof(utcb->msg[0]));

	/* copy typed items to utcb of global worker */
	for (unsigned i=0; i < typed_items - 1; i++) {
		struct Utcb::Item * item_caller = utcb->get_item(i);
		struct Utcb::Item * item_worker = utcb_worker->get_item(i);
	
		/* map/translate items */
		*item_worker = *item_caller;
		item_worker->hotspot = (item_worker->hotspot & ~0x3UL) | 0x2UL;
	}

	/* tell global worker the number of untyped items */
	utcb_worker->items = ((typed_items - 1) << 16UL) | (untyped - 2);
	/* tell global worker about job information and about this thread */
	worker->job(crd.base(), block_sm, utcb, rcv_wnd, utcb_worker);

	/* wake up worker that actually does the IPC */	
	worker->cancel_blocking();

	/* wait until global worker thread is done */
	if (NOVA_OK != sm_ctrl(block_sm, SEMAPHORE_DOWNZERO))
		*reinterpret_cast<unsigned long*>(~0UL) = 0;

	/* worker is available again */
	Xcpu_ipc::release_worker(worker);
}
