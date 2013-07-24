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

class Xcpu_thread : public Genode::Thread<4096> {

	private:

		unsigned _cpu;

		struct job {
			addr_t call_pt;
			addr_t wakeup_sm;
			Nova::Utcb * applicant_utcb;
		} __attribute__((packed));

		static void _thread_start()
		{
			Thread_base::myself()->entry();
			sleep_forever();
		}

		static void _xcpu_startup_handler() {
			using namespace Nova;

			Utcb * utcb = reinterpret_cast<Utcb *>(Thread_base::myself()->utcb());

			utcb->ip = *reinterpret_cast<addr_t *>(utcb->sp);
			utcb->mtd = Mtd::EIP | Mtd::ESP;
			utcb->set_msg_word(0);

			reply(Thread_base::myself()->stack_top());
		}


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

		void entry() {

			using namespace Nova;

			Utcb * utcb = reinterpret_cast<Utcb *>(myself()->utcb());

			while(1) {

				if (NOVA_OK != sm_ctrl(_tid.exc_pt_sel + SM_SEL_EC, SEMAPHORE_DOWNZERO))
					*reinterpret_cast<unsigned long*>(~0UL) = 0;

				_make_ipc(utcb);
			}
		}

		/**
		 * Constructor
		 */
		explicit Xcpu_thread(unsigned cpu) : Thread("IPC XCPU "), _cpu(cpu) {
			unsigned len = strlen(_context->name);
			snprintf(_context->name + len, Context::NAME_LEN - len, "%u", cpu);

			using namespace Nova;
			addr_t pd_sel   = Platform_pd::pd_core_sel();
			addr_t utcb = reinterpret_cast<addr_t>(&_context->utcb);

			/*
			 * Put IP on stack, it will be read from core pager in platform.cc
			 */
			addr_t *sp   = reinterpret_cast<addr_t *>(_context->stack - sizeof(addr_t));
			*sp = reinterpret_cast<addr_t>(_thread_start);
	
			/* create global EC */
			enum { GLOBAL = true };
			uint8_t res = create_ec(_tid.ec_sel, pd_sel, _cpu,
			                        utcb, (mword_t)sp, _tid.exc_pt_sel, GLOBAL);
			if (res != NOVA_OK) {
				PERR("%p - create_ec returned %d", this, res);
				throw Cpu_session::Thread_creation_failed();
			}

			/* default: we don't accept any mappings or translations */
			Utcb * utcb_obj = reinterpret_cast<Utcb *>(Thread_base::utcb());
			utcb_obj->crd_rcv = Obj_crd();
			utcb_obj->crd_xlt = Obj_crd();

			/*
			 * Startup portal and page fault portal of main thread can't
			 * be reused if we are on another CPU.
			 */
			if (_cpu != boot_cpu())
				return;

			/* remap startup portal from main thread */
			if (map_local((Utcb *)Thread_base::myself()->utcb(),
			              Obj_crd(PT_SEL_STARTUP, 0),
			              Obj_crd(_tid.exc_pt_sel + PT_SEL_STARTUP, 0))) {
				PERR("could not create startup portal");
				throw Cpu_session::Thread_creation_failed();
			}

			/* remap debugging page fault portal for core threads */
			if (map_local((Utcb *)Thread_base::myself()->utcb(),
			              Obj_crd(PT_SEL_PAGE_FAULT, 0),
			              Obj_crd(_tid.exc_pt_sel + PT_SEL_PAGE_FAULT, 0))) {
				PERR("could not create page fault portal");
				throw Cpu_session::Thread_creation_failed();
			}
		}


		void start()
		{
			using namespace Nova;

			/* set _thread_cap to signal we are alive */
			_thread_cap = reinterpret_cap_cast<Cpu_thread>(Native_capability(_tid.ec_sel));

			addr_t const pd_sel   = Platform_pd::pd_core_sel();

			/* create SC */
			unsigned sc_sel = cap_selector_allocator()->alloc();
			uint8_t res = create_sc(sc_sel, pd_sel, _tid.ec_sel, Qpd());
			if (res != NOVA_OK) {
				PERR("%p - create_sc returned returned %d", this, res);
				throw Cpu_session::Thread_creation_failed();
			}
		}


		uint8_t create_startup_portal(addr_t ec_sel)
		{
			using namespace Nova;

			if (_cpu == boot_cpu())
				return NOVA_OK;

			addr_t const pd_sel   = Platform_pd::pd_core_sel();
			addr_t const pt_sel   = _tid.exc_pt_sel + PT_SEL_STARTUP;
			addr_t const eip = reinterpret_cast<addr_t>(_xcpu_startup_handler);

			uint8_t res = create_pt(pt_sel, pd_sel, ec_sel,
			                        Mtd(Mtd::ESP | Mtd::EIP), eip);
			if (res == NOVA_OK)
				revoke(Obj_crd(pt_sel, 0, Obj_crd::RIGHT_PT_CTRL));

			return res; 
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

		unsigned cpu() { return _cpu; }
};

class Xcpu_ipc {

	private:

		enum { MAX_SUPPORTED_CPUS = 64 };

		static struct worker {
			Xcpu_thread * worker;
			Genode::Lock _lock;
		} global_worker[MAX_SUPPORTED_CPUS];

	public:

		static void handle(Nova::Utcb * utcb, addr_t block_sm,
		                   Nova::Obj_crd rcv_wnd, addr_t base_sel,
		                   addr_t sp_high, addr_t sp_low);


		static void init()
		{
			unsigned cpu = 0;
			try {
				for (cpu = 0; cpu < MAX_SUPPORTED_CPUS; cpu++)
					global_worker[cpu].worker = new (Genode::env()->heap()) Xcpu_thread(cpu);
			} catch (Genode::Cpu_session::Thread_creation_failed) { }

			/* report the number of CPUs available for XCPU IPC */
			PINF("xCPU IPC support enabled for %u CPUs.", cpu); 
		}


		static void check_spawn_worker(unsigned cpu, addr_t ec_sel) 
		{	
			if ((cpu < MAX_SUPPORTED_CPUS) && global_worker[cpu].worker &&
			    !global_worker[cpu].worker->cap().valid()) {

				PINF("start a xCPU IPC thread on CPU %u", cpu);

				uint8_t const res = global_worker[cpu].worker->create_startup_portal(ec_sel);
				if (res == Nova::NOVA_OK)
					global_worker[cpu].worker->start();
				else
					PERR("could not create startup portal of xCPU IPC thread, "
					     "error = %u\n", res);
			}
		}
	
	
		static Xcpu_thread * worker(unsigned cpu) {
			if (cpu >= MAX_SUPPORTED_CPUS)
				return 0;

			global_worker[cpu]._lock.lock();

			return global_worker[cpu].worker;
		}


		static void release_worker(Xcpu_thread * xcpu) {
			global_worker[xcpu->cpu()]._lock.unlock(); }
};

};

#endif /* _BASE_PAGER_XCPU_H_ */
