/*
 * \brief  NOVAe-specific support code for the server-side RPC API
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \author Alexander Boettcher
 * \date   2010-01-13
 */

/*
 * Copyright (C) 2010-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/rpc_server.h>
#include <base/env.h>
#include <base/sleep.h>

/* base-internal includes */
#include <base/internal/stack.h>
#include <base/internal/ipc.h>

/* NOVAe includes */
#include <novae/util.h>
#include <novae/native_thread.h>

using namespace Genode;


/***********************
 ** Server entrypoint **
 ***********************/

Untyped_capability Rpc_entrypoint::_manage(Rpc_object_base *obj)
{
	using namespace Novae;

	/* don't manage RPC object twice */
	if (obj->cap().valid()) {
		warning("attempt to manage RPC object twice");
		return obj->cap();
	}

	Untyped_capability ec_cap;

	/* _ec_sel is invalid until thread gets started */
	if (native_thread().ec_sel != Native_thread::INVALID_INDEX)
		ec_cap = Capability_space::import(native_thread().ec_sel);
	else
		ec_cap = Thread::cap();

	Untyped_capability obj_cap = _alloc_rpc_cap(_pd_session, ec_cap,
	                                            (addr_t)&_activation_entry);

	if (!obj_cap.valid())
		return obj_cap;

	/* add server object to object pool */
	obj->cap(obj_cap);
	insert(obj);

	/* return object capability managed by entrypoint thread */
	return obj_cap;
}

static void cleanup_call(Rpc_object_base *obj, Novae::Utcb * ep_utcb,
                         Native_capability &cap)
{

	/* effectively invalidate the capability used before */
	obj->cap(Untyped_capability());

	/*
	 * The activation may execute a blocking operation in a dispatch function.
	 * Before resolving the corresponding object, we need to ensure that it is
	 * no longer used by an activation. Therefore, we to need cancel an
	 * eventually blocking operation and let the activation leave the context
	 * of the object.
	 */
	using namespace Novae;

	Utcb *utcb = reinterpret_cast<Utcb *>(Thread::myself()->utcb());
	/* don't call ourself */
	if (utcb == ep_utcb)
		return;

	/* make a IPC to ensure that cap() identifier is not used anymore */
	unsigned mtd   = 0;
	utcb->msg()[0] = 0xdead;
	if (uint8_t res = call(cap.local_name(), mtd))
		error(utcb, " - could not clean up entry point of thread ", ep_utcb, " - res ", res);
}

void Rpc_entrypoint::_dissolve(Rpc_object_base *obj)
{
	/* don't dissolve RPC object twice */
	if (!obj || !obj->cap().valid())
		return;

	/* de-announce object from cap_session, revoke implicitly assumed XXX */
	_free_rpc_cap(_pd_session, obj->cap());

	/* make sure nobody is able to find this object */
	remove(obj);

	cleanup_call(obj, reinterpret_cast<Novae::Utcb *>(this->utcb()), _cap);
}

static void reply(auto const id_pt, auto const transaction_id,
                  Thread &thread, Novae::Utcb &utcb,
                  Rpc_exception_code exc, Msgbuf_base &snd_msg)
{
	auto pt    = thread.native_thread().exc_pt_sel + Novae::PT_SEL_DELEGATE;
	auto count = copy_msgbuf_to_utcb(id_pt, transaction_id, pt,
	                                 utcb, snd_msg, exc.value);

	Novae::reply(thread.stack_top(), count ? count - 1 : 0);
}

extern addr_t __initial_sp;
extern addr_t __initial_di;
extern addr_t __initial_si;

void Rpc_entrypoint::_activation_entry()
{
	/* retrieve portal id from RDI and mtd form RSI */
	addr_t id_pt; asm volatile ("" : "=D" (id_pt));
	addr_t   mtd; asm volatile ("" : "=S" (mtd));

	Thread         &thread = *static_cast<Thread *>(Thread::myself());
	Rpc_entrypoint &ep     = *static_cast<Rpc_entrypoint *>(Thread::myself());
	Novae::Utcb    &utcb   = *reinterpret_cast<Novae::Utcb *>(Thread::myself()->utcb());

	auto const transaction_id = utcb.msg()[0];

	/* handle ill-formed message */
	if (mtd + 1 < 2) {
		ep._rcv_buf.word(0) = ~0UL; /* invalid opcode */
	} else {
		copy_utcb_to_msgbuf(transaction_id, utcb, ep._rcv_buf, mtd + 1,
		                    Msgbuf_base::MAX_CAPS_PER_MSG);
	}

	Ipc_unmarshaller unmarshaller(ep._rcv_buf);

	Rpc_opcode opcode(0);
	unmarshaller.extract(opcode);

	/* default return value */
	Rpc_exception_code exc = Rpc_exception_code(Rpc_exception_code::INVALID_OBJECT);

	/* in case of a portal cleanup call we are done here - just reply */
	if (ep._cap.local_name() == long(id_pt)) {
		ep._rcv_buf.reset();
		reply(id_pt, transaction_id + 1, thread, utcb, exc, ep._snd_buf);
	}

	/* atomically lookup and lock referenced object */
	auto lambda = [&] (Rpc_object_base *obj)
	{
		if (!obj) {
			error("could not look up server object, return from call id_pt=", id_pt);
			return;
		}

		/* dispatch request */
		ep._snd_buf.reset();

		exc = obj->dispatch(opcode, unmarshaller, ep._snd_buf);
	};
	ep.apply(id_pt, lambda);

	ep._rcv_buf.reset();
	reply(id_pt, transaction_id + 1, thread, utcb, exc, ep._snd_buf);
}


void Rpc_entrypoint::entry()
{
	/*
	 * Thread entry is not used for activations on NOVAe
	 */
}


void Rpc_entrypoint::_block_until_cap_valid() { }


bool Rpc_entrypoint::is_myself() const
{
	return (Thread::myself() == this);
}


Rpc_entrypoint::Rpc_entrypoint(Pd_session *pd_session, size_t stack_size,
                               const char *name, Affinity::Location location)
:
	Thread(Cpu_session::Weight::DEFAULT_WEIGHT, name, stack_size, location),
	_pd_session(*pd_session)
{
	/* set magic value evaluated by thread_nova.cc to start a local thread */
	if (native_thread().ec_sel == Native_thread::INVALID_INDEX) {
		native_thread().ec_sel = Native_thread::INVALID_INDEX - 1;
		native_thread().initial_ip = (addr_t)&_activation_entry;
	}

	/* required to create a 'local' EC */
	Thread::start();

	/* create cleanup portal */
	_cap = _alloc_rpc_cap(_pd_session,
	                      Capability_space::import(native_thread().ec_sel),
	                      (addr_t)_activation_entry);
	if (!_cap.valid()) {
		error("failed to allocate RPC cap for new entrypoint");
		return;
	}
}


Rpc_entrypoint::~Rpc_entrypoint()
{
	using Pool = Object_pool<Rpc_object_base>;

	Pool::remove_all([&] (Rpc_object_base *obj) {
		warning("object pool not empty in ", __func__);

		/* don't dissolve RPC object twice */
		if (!obj || !obj->cap().valid())
			return;

		/* de-announce object from cap_session, revoke implicitly assumed XXX */
		_free_rpc_cap(_pd_session, obj->cap());

		cleanup_call(obj, reinterpret_cast<Novae::Utcb *>(this->utcb()), _cap);
	});

	if (!_cap.valid())
		return;

	/* de-announce _cap, revoke implicitly assumed XXX */
	_free_rpc_cap(_pd_session, _cap);
}


void Rpc_entrypoint::reply_signal_info(Untyped_capability reply_cap,
                                       unsigned long imprint, unsigned long cnt)
{
	(void)reply_cap;
	(void)imprint;
	(void)cnt;

	error(__func__, " not implemented");
	sleep_forever();
	/*
	Msgbuf<sizeof(Signal_source::Signal)> snd_buf;
	snd_buf.insert(Signal_source::Signal(imprint, (int)cnt));
	ipc_reply(reply_cap, Rpc_exception_code(Rpc_exception_code::SUCCESS), snd_buf);
	*/
}
