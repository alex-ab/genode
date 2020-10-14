/*
 * \brief  CPU session definition
 * \author Alexander Boettcher
 * \date   2020-07-16
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SESSION_H_
#define _SESSION_H_

/* Genode includes */
#include <base/env.h>
#include <base/heap.h>
#include <base/rpc_server.h>
#include <base/trace/types.h>
#include <cpu_session/client.h>
#include <os/reporter.h>

#include "policy.h"

namespace Cpu {
	using namespace Genode;

	using Genode::Trace::Subject_id;

	class Session;
	class Trace;
	class Policy;
	struct ThreadX;
	typedef Id_space<Parent::Client>::Element Client_id;
	typedef List<List_element<Session> > Child_list;
	typedef List<List_element<ThreadX> > Thread_list;
	typedef Constrained_ram_allocator Ram_allocator;
}

struct Cpu::ThreadX
{
		List_element<ThreadX>  _list_element { this };

		Thread_capability      _cap    { };
		Genode::Thread::Name   _name   { };
		Subject_id             _id     { };
		Cpu::Policy *          _policy { nullptr };
};

class Cpu::Session : public Rpc_object<Cpu_session>
{
	private:

		List_element<Session>  _list_element { this };

		Child_list            &_list;
		Env                   &_env;

		Ram_quota_guard        _ram_guard;
		Cap_quota_guard        _cap_guard;
		Ram_allocator          _ram      { _env.pd(), _ram_guard, _cap_guard };
		Heap                   _md_alloc { _ram, _env.rm() };

		Parent::Client         _parent_client { };
		Client_id const        _id            { _parent_client,
		                                        _env.id_space() };
		Cpu_session_client     _parent;

		Session::Label   const _label;
		Affinity         const _affinity;

		Thread_list            _threads { };

		bool                   _report  { true  };
		bool                   _verbose;

		void construct_policy(Thread::Name const &name, Cpu::Policy **policy,
		                      Affinity::Location const loc)
		{
			if (name == "pin")
				*policy = new (_md_alloc) Policy_pin();
			else if (name == "round-robin")
				*policy = new (_md_alloc) Policy_round_robin();
			else if (name == "max-utilize")
				*policy = new (_md_alloc) Policy_max_utilize();
			else
				*policy = new (_md_alloc) Policy_none();

			(*policy)->location = loc;
		}

		template <typename FUNC>
		void _for_each_thread(FUNC const &fn)
		{
			for (auto t = _threads.first(); t; t = t->next()) {
				auto thread = t->object();
				if (!thread)
					continue;

				bool done = fn(thread);
				if (done)
					break;
			}
		}

		template <typename FUNC>
		void kill(Thread_capability const &cap, FUNC const &fn)
		{
			_for_each_thread([&](ThreadX *thread) {
				if (!(thread->_cap.valid()) || !(thread->_cap == cap))
					return false;

				fn(thread->_cap, thread->_name, thread->_id, *thread->_policy);

				destroy(_md_alloc, thread->_policy);
				_threads.remove(&thread->_list_element);
				destroy(_md_alloc, thread);

				return true;
			});
		}

		template <typename FUNC>
		void apply(FUNC const &fn)
		{
			_for_each_thread([&](ThreadX *thread) {
				if (!thread->_cap.valid())
					return false;

				return fn(thread->_cap, thread->_name,
				          thread->_id, *thread->_policy);
			});
		}

		template <typename FUNC>
		void lookup(Thread::Name const &name, FUNC const &fn)
		{
			if (!name.valid())
				return;

			_for_each_thread([&](ThreadX *thread) {
				if (thread->_name != name)
					return false;

				return fn(thread->_cap, *thread->_policy);
			});
		}

		template <typename FUNC>
		void reconstruct(Cpu::Policy::Name const &policy_name,
		                 Thread::Name const &thread_name,
		                 FUNC const &fn)
		{
			if (!thread_name.valid())
				return;

			bool done = false;

			_for_each_thread([&](ThreadX *thread) {
				if (thread->_name != thread_name)
					return false;

				bool same = thread->_policy->same_type(policy_name);

				if (!same) {
					Affinity::Location const location = thread->_policy->location;
					destroy(_md_alloc, thread->_policy);
					thread->_policy = nullptr; /* in case construct_policy throws */
					construct_policy(policy_name, &thread->_policy, location);
				}

				fn(thread->_cap, *thread->_policy);
				done = true;
				return true;
			});

			if (done)
				return;

			construct(policy_name, [&](Thread_capability const &cap,
			                           Thread::Name &store_name,
			                           Cpu::Policy &policy) {
				store_name == thread_name;
				fn(cap, policy);
			});
		}

		template <typename FUNC>
		void construct(Cpu::Policy::Name const &policy_name, FUNC const &fn)
		{
			ThreadX * thread = new (_md_alloc) ThreadX();

			_threads.insert(&thread->_list_element);

			construct_policy(policy_name, &thread->_policy, Affinity::Location());

			fn(thread->_cap, thread->_name, *thread->_policy);
		}

		void _schedule(Thread_capability const &, Affinity::Location const &,
		               Cpu::Policy const &);

		/*
		 * Noncopyable
		 */
		Session(Session const &);
		Session &operator = (Session const &);

	public:

		/**
		 * Constructor
		 */
		Session(Env &, Affinity const &, Root::Session_args const &,
		        Child_list &, bool);

		~Session();


		/***************************
		 ** CPU session interface **
		 ***************************/

		Thread_capability create_thread(Pd_session_capability,
		                                Thread::Name const &,
		                                Affinity::Location, Weight,
		                                addr_t) override;
		void kill_thread(Thread_capability) override;
		void exception_sigh(Signal_context_capability) override;
		Affinity::Space affinity_space() const override;
		Dataspace_capability trace_control() override;
		int ref_account(Cpu_session_capability) override;
		int transfer_quota(Cpu_session_capability, size_t) override;
		Quota quota() override;
		Capability<Cpu_session::Native_cpu> native_cpu() override;

		/******
		 **  **
		 ******/
		bool match(Label const &label) const { return _label == label; };
		void config(Thread::Name const &, Cpu::Policy::Name const &,
		            Affinity::Location const &);
		void iterate_threads();
		void iterate_threads(Trace &, Session_label const &);
		void report_state(Xml_generator &);
		bool report_update() const { return _report; }

		template <typename FUNC>
		void upgrade(FUNC const &fn) {
			Genode::warning("upgrade of session");
			fn(_id.id());
		}
};

#endif /* _SESSION_H_ */
