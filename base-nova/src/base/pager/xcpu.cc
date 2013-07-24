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

/* Core includes */
#include <platform.h>

/* NOVA includes */
#include <nova/syscalls.h>

#include "xcpu.h"

using namespace Genode;

struct Xcpu_ipc::worker Xcpu_ipc::global_worker[Xcpu_ipc::MAX_SUPPORTED_CPUS];

extern Genode::addr_t __core_pd_sel;

void Xcpu_ipc::handle(Nova::Utcb * utcb, addr_t block_sm,
                      Nova::Obj_crd rcv_wnd, addr_t base_sel,
                      addr_t stack_high, addr_t stack_low)
{
	enum { GLOBAL_THREAD = true, UTCB_SIZE = 4096, NO_STARTUP_EXC = 1 };

	using namespace Nova;

	unsigned typed_items = utcb->msg_items();
	unsigned untyped     = utcb->msg_words();

	/* last item refers to the portal to be called actually */
	struct Utcb::Item * item = utcb->get_item(typed_items - 1);

	addr_t target_cpu = utcb->msg[untyped - 2];

	void * virt_ptr = 0;
	Genode::platform()->region_alloc()->alloc(UTCB_SIZE, &virt_ptr);

	Crd crd(item->crd);
	utcb->msg[0]      = utcb->msg[untyped - 1];

	/* create worker on the fly */
	addr_t const ec_sel = base_sel;
	addr_t const sc_sel = base_sel + 1;
	addr_t const sm_sel = base_sel + 2;
	addr_t const start_eip = reinterpret_cast<addr_t>(Xcpu_thread::startup);
	addr_t const virt_utcb = reinterpret_cast<addr_t>(virt_ptr);
	Nova::Utcb * utcb_worker = reinterpret_cast<Nova::Utcb *>(virt_utcb);
	uint8_t res = NOVA_INV_CPU;

	if (!item || item->is_del() || !virt_ptr)
		goto clean_region;

	res = create_ec(ec_sel, __core_pd_sel, target_cpu, virt_utcb,
	                stack_high, start_eip, GLOBAL_THREAD, NO_STARTUP_EXC);
	if (res != NOVA_OK)
		goto clean_region;

	res = create_sm(sm_sel, __core_pd_sel, 0);
	if (res != NOVA_OK)
		goto clean_ec;

	/* tell worker thread its own utcb */
	*reinterpret_cast<volatile addr_t *>(stack_high) = virt_utcb;

	/* copy untyped items/words to utcb of global worker */
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
	/* tell global worker thread the semaphore it finally can block on */
	utcb_worker->tls   = sm_sel;
	/* tell global worker about job information and about this thread */
	Xcpu_thread::job(crd.base(), block_sm, utcb, rcv_wnd, utcb_worker);

	/* let worker run */
	res = create_sc(sc_sel, __core_pd_sel, ec_sel, Qpd());
	if (res != NOVA_OK)
		goto clean_ec;

	/* wait until global worker thread is done */
	res = sm_ctrl(block_sm, SEMAPHORE_DOWNZERO);
	if (res != NOVA_OK); 
		goto clean_ec;

	/* sanity check for stack overrun and report it */
	if (utcb->msg[utcb->msg_words()] < stack_low)
		PERR("stack overrun of xCPU IPC thread detected "
		     "- [0x%lx-0x%lx] vs 0x%lx", stack_low, stack_high,
		     utcb->msg[utcb->msg_words()]);

clean_ec:

	/* destroy the EC, SC and SM of the worker thread */
	revoke(Obj_crd(ec_sel, 0), true);
	revoke(Obj_crd(sc_sel, 0), true);
	revoke(Obj_crd(sm_sel, 0), true);

	/* get rid of the UTCB */
	revoke(Mem_crd(virt_utcb >> 12, 0, Rights(true, true, true)), true);
	Genode::platform()->region_alloc()->free(virt_ptr, UTCB_SIZE);

clean_region:

	if (virt_ptr)
		Genode::platform()->region_alloc()->free(virt_ptr, UTCB_SIZE);

	if (res != NOVA_OK) {
		PERR("xCPU IPC failed - target cpu %lu, error %x", target_cpu, res);
		utcb->set_msg_word(0);
	}
}
