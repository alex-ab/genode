/*
 * \brief  CPU session implementation
 * \author Alexander Boettcher
 * \date   2020-07-16
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include "session.h"

using namespace Genode;
using namespace Cpu;

Thread_capability
Cpu::Session::create_thread(Pd_session_capability const  pd,
                            Name                  const &name_by_client,
                            Affinity::Location    const  location,
                            Weight                const  weight,
                            addr_t                const  utcb)
{
	Thread_capability cap { };

	Name name = name_by_client;
	if (!name.valid())
		name = Name("nobody");

	lookup(name, [&](Thread_capability &store_cap,
	                 Affinity::Location &store_loc,
	                 uint8_t const &policy)
	{
		if (store_cap.valid())
			return false;

		cap = _parent.create_thread(pd, name, location, weight, utcb);
		if (!cap.valid())
			/* stop creation attempt by saying done */
			return true;

		/* policy and name are set beforehand */
		store_cap  = cap;

		/* for static case with valid location, don't overwrite config */
		if ((policy != POLICY_PIN) ||
		    (store_loc.width() * store_loc.height() == 0))
			store_loc = location;

		return true;
	});

	if (cap.valid()) {
		Genode::warning("create found ", location.xpos(), " ",
		                location.ypos(), " ", name);
		return cap;
	}

	/* unknown thread without any configuration */
	construct([&](Thread_capability &store_cap,
	              Affinity::Location &store_loc,
	              Name &store_name, uint8_t &store_policy)
	{
		cap = _parent.create_thread(pd, name, location, weight, utcb);

		if (!cap.valid())
			/* stop creation attempt by saying done */
			return true;

		store_cap = cap;
		store_loc = location;
		store_name = name;
		store_policy = POLICY_NONE;
		return true;
	});

	Genode::warning("create unknown ", location.xpos(), " ", location.ypos(), " ", name);
	return cap;
}


void Cpu::Session::kill_thread(Thread_capability const thread_cap)
{
	if (!thread_cap.valid())
		return;

	_parent.kill_thread(thread_cap);

	apply([&](Thread_capability &cap, Affinity::Location &,
	          Name &name, uint8_t &policy, Subject_id &)
	{
		if (!(cap == thread_cap))
			return false;

		cap  = Thread_capability();
		name = Thread::Name();
		policy = 0;
		return true;
	});
}

void Cpu::Session::exception_sigh(Signal_context_capability const h)
{
	_parent.exception_sigh(h);
}

Affinity::Space Cpu::Session::affinity_space() const
{
	return _parent.affinity_space();
}

Dataspace_capability Cpu::Session::trace_control()
{
	return _parent.trace_control();
}

Cpu::Session::Session(Env &env,
                      Affinity const &affinity,
                      Root::Session_args const &args,
                      Child_list &list)
:
	_env(env),
	_parent(_env.session<Cpu_session>(_id.id(), args, affinity)),
	_list(list),
	_label(session_label_from_args(args.string())),
	_affinity(affinity)
{
	try {
		_env.ep().rpc_ep().manage(this);
	} catch (...) {
		env.close(_id.id());
		throw;
	}

	_list.insert(&_list_element);
}

Cpu::Session::~Session()
{
	Genode::warning(__func__, " missing");

	_list.remove(&_list_element);

	_env.ep().rpc_ep().dissolve(this);

	_env.close(_id.id());
}

int Cpu::Session::ref_account(Cpu_session_capability const cap)
{
	return _parent.ref_account(cap);
}

int Cpu::Session::transfer_quota(Cpu_session_capability const cap,
                                 size_t const size)
{
	return _parent.transfer_quota(cap, size);
}

Cpu_session::Quota Cpu::Session::quota()
{
	return _parent.quota();
}

Capability<Cpu_session::Native_cpu> Cpu::Session::native_cpu()
{
	return _parent.native_cpu();
}

void Cpu::Session::report_state(Xml_generator &xml)
{
		xml.node("component", [&] () {
			xml.attribute("xpos",   _affinity.location().xpos());
			xml.attribute("ypos",   _affinity.location().ypos());
			xml.attribute("width",  _affinity.location().width());
			xml.attribute("height", _affinity.location().height());
			xml.attribute("label",  _label);

			apply([&](Thread_capability &,
			          Affinity::Location const &location,
			          Thread::Name const &name, uint8_t const &policy,
			          Subject_id const &)
			{
				xml.node("thread", [&] () {
					xml.attribute("xpos", location.xpos());
					xml.attribute("ypos", location.ypos());
					xml.attribute("policy", policy);
					xml.attribute("name", name);
				});
				return false;
			});
		});

	/* reset */
	_report = false;
}
