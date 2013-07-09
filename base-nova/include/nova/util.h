/*
 * \brief  Helper code used by core as base framework
 * \author Alexander Boettcher
 * \date   2012-08-08
 */

/*
 * Copyright (C) 2012-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _NOVA__INCLUDE__UTIL_H_
#define _NOVA__INCLUDE__UTIL_H_

#include <base/printf.h>
#include <base/thread.h>

__attribute__((always_inline))
inline void nova_die(const char * text = 0)
{
	/*
	 * If thread is de-constructed the sessions are already gone.
	 * Be careful when enabling printf here.
	 */
	while (1)
		asm volatile ("ud2a" : : "a"(text));
}


inline void request_event_portal(Genode::Native_capability cap,
                                 Genode::addr_t exc_base, Genode::addr_t event,
                                 Genode::Native_capability cap_param = Genode::Native_capability())
{
	using namespace Nova;
	Utcb *utcb = (Utcb *)Genode::Thread_base::myself()->utcb();

	if (!cap_param.valid())
		cap_param = cap;

	/* save original receive windows */
	Crd orig_crd_rcv = utcb->crd_rcv;
	Crd orig_crd_xlt = utcb->crd_xlt;

	/* request event-handler portal */
	utcb->crd_rcv = Obj_crd(exc_base + event, 0);
	utcb->msg[0]  = event;
	utcb->set_msg_word(1);

	/* use pager cap as parameter and translate during IPC */ 
	enum { FROM_OWN_PD = false, NO_GUEST_VM = false, TRANSLATE_MAP = true };
	uint8_t res = utcb->append_item(Obj_crd(cap_param.local_name(), 0), 0,
	                                FROM_OWN_PD, NO_GUEST_VM, TRANSLATE_MAP);
	/* one item fits on the UTCB */
	(void)res;

	res = call(cap.local_name());
	if (res != NOVA_OK || utcb->msg_items() != 1 ||
	    !utcb->get_item(0)->is_del() || Crd(utcb->get_item(0)->crd).is_null())
	{
		PERR("request of exception portal %lx failed, state %u:%u:%u:%u:0x%lx",
		     event, res, utcb->msg_items(), utcb->get_item(0)->is_del(),
	         Crd(utcb->get_item(0)->crd).is_null(), cap_param.local_name());
	}

	/* restore original windows */
	utcb->crd_rcv = orig_crd_rcv;
	utcb->crd_xlt = orig_crd_xlt;
}

#endif /* _NOVA__INCLUDE__UTIL_H_ */
