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

#ifndef _BASE_PAGER_XCPU_H_
#define _BASE_PAGER_XCPU_H_

/* Genode base framework includes */
#include <base/thread.h>
#include <base/sleep.h>
#include <base/snprintf.h>
#include <base/env.h>

/* Genode core includes */
#include <platform_pd.h>

/* NOVA specific includes */
#include <nova_util.h>

namespace Genode {

class Xcpu_ipc {

	private:

		struct job {
			addr_t call_pt;
			addr_t wakeup_sm;
			Nova::Utcb * applicant_utcb;
		} __attribute__((packed));


		static void _make_ipc(Nova::Utcb * utcb) {

			using namespace Nova;

			/* make a copy of the job structure on the stack */
			struct job job = *reinterpret_cast<struct job *>(&utcb->msg[utcb->msg_words()]);

			uint8_t res = call(job.call_pt);
			if (res != NOVA_OK) {
				job.applicant_utcb->items = 0;
				goto get_out;
			}

			/* copy untyped items onto applicant's/caller's utcb */
			memcpy(job.applicant_utcb->msg, utcb->msg,
			       utcb->msg_words() * sizeof(utcb->msg[0]));

			/* copy typed items to utcb of global worker */
			for (unsigned i = 0; i < utcb->msg_items(); i++) {
				struct Utcb::Item * item_worker = utcb->get_item(i);
				struct Utcb::Item * item_caller = job.applicant_utcb->get_item(i);
	
				/* map/delegate item */
				*item_caller = *item_worker;
				item_caller->hotspot = (item_worker->hotspot & ~0x3UL) | 2UL;
			}

			/* tell global worker the number of untyped items */
			job.applicant_utcb->items = utcb->items;

			get_out:

			/* put stack pointer for sanity checking by caller on his utcb */ 
			job.applicant_utcb->msg[utcb->msg_words()] = (addr_t)&job;

			/* wakeup the applicant */
			sm_ctrl(job.wakeup_sm, SEMAPHORE_UP);
		}

	public:

		static void startup() {
			using namespace Nova;
			Utcb * utcb; 
			addr_t addr = reinterpret_cast<addr_t>(&utcb);
			addr = (addr + 0x1F) & ~0xFUL;
			utcb = reinterpret_cast<Utcb *>(*reinterpret_cast<volatile addr_t *>(addr));

			_make_ipc(utcb);

			/* we will get be deleted by the caller and by the kernel */
			while(1) { sm_ctrl(utcb->tls, SEMAPHORE_DOWNZERO); }
		}


		/**
		 * Store information about whom to call and about the applicant, so
		 * that we can notify him when the IPC is done.
		 */
		static void job(addr_t call_pt, addr_t wakeup_sm, Nova::Utcb * applicant_utcb,
		         Nova::Obj_crd rcv_wnd, Nova::Utcb * utcb_worker)
		{
			void * ptr = &utcb_worker->msg[utcb_worker->msg_words()];
			struct job * message = reinterpret_cast<struct job *>(ptr);

			message->call_pt = call_pt;
			message->wakeup_sm = wakeup_sm;
			message->applicant_utcb = applicant_utcb;

			utcb_worker->crd_rcv = rcv_wnd;
		}


		static void handle(Nova::Utcb * utcb, addr_t block_sm,
		                   Nova::Obj_crd rcv_wnd, addr_t base_sel,
		                   addr_t sp_high, addr_t sp_low);
};

}

#endif /* _BASE_PAGER_XCPU_H_ */
