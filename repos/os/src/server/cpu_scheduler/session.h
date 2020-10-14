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
	typedef Id_space<Parent::Client>::Element Client_id;
	typedef List<List_element<Session> > Child_list;
}

class Cpu::Session : public Rpc_object<Cpu_session>
{
	private:

		Env                       &_env;
		Ram_quota_guard            _ram_guard;
		Cap_quota_guard            _cap_guard;
		Constrained_ram_allocator  _ram      { _env.pd(), _ram_guard, _cap_guard };
		Heap                       _md_alloc { _ram, _env.rm() };

		Parent::Client         _parent_client { };
		Client_id const        _id            { _parent_client,
		                                        _env.id_space() };
		Cpu_session_client     _parent;

		Child_list            &_list;
		List_element<Session>  _list_element { this };

		Session::Label         _label;
		Affinity         const _affinity;

		enum { MAX_THREADS = 32 };

		Thread_capability      _threads    [MAX_THREADS] { };
		Thread::Name           _names      [MAX_THREADS] { };
		Subject_id             _subject_id [MAX_THREADS] { };
		Cpu::Policy *          _pol        [MAX_THREADS] { };

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
		void kill(Thread_capability const cap, FUNC const &fn)
		{
			for (unsigned i = 0; i < MAX_THREADS; i++) {
				if (!_threads[i].valid() || !(_threads[i] == cap))
					continue;

				fn(_threads[i], _names[i], _subject_id[i], *_pol[i]);

				destroy(_md_alloc, _pol[i]);
				_pol[i] = nullptr;
				break;
			}
		}

		template <typename FUNC>
		void apply(FUNC const &fn)
		{
			for (unsigned i = 0; i < MAX_THREADS; i++) {
				if (!_threads[i].valid())
					continue;

				bool const done = fn(_threads[i], _names[i],
				                     _subject_id[i], *_pol[i]);
				if (done)
					break;
			}
		}

		template <typename FUNC>
		void lookup(Thread::Name const &name, FUNC const &fn)
		{
			if (!name.valid())
				return;

			for (unsigned i = 0; i < MAX_THREADS; i++) {
				if (_names[i] != name)
					continue;

				if (fn(_threads[i], *_pol[i]))
					break;
			}
		}

		template <typename FUNC>
		void reconstruct(Cpu::Policy::Name const &policy_name,
		                 Thread::Name const &thread_name,
		                 FUNC const &fn)
		{
			if (!thread_name.valid())
				return;

			unsigned free = ~0U;

			for (unsigned i = 0; i < MAX_THREADS; i++) {
				if (free >= MAX_THREADS && !_threads[i].valid() && !_names[i].valid())
					free = i;

				if (_names[i] != thread_name)
					continue;

				bool same = _pol[i]->same_type(policy_name);

				if (!same) {
					Affinity::Location const location = _pol[i]->location;
					destroy(_md_alloc, _pol[i]);
					_pol[i] = nullptr; /* in case construct_policy throws */
					construct_policy(policy_name, _pol + i, location);
				}

				fn(_threads[i], *_pol[i]);
				return;
			}

			if (free < MAX_THREADS) {
				construct_policy(policy_name, _pol + free, Affinity::Location());

				_names[free] = thread_name;

				fn(_threads[free], *_pol[free]);
			}
		}

		template <typename FUNC>
		void construct(Cpu::Policy::Name const &policy_name, FUNC const &fn)
		{
			for (unsigned i = 0; i < MAX_THREADS; i++) {
				if (_threads[i].valid() || _names[i].valid())
					continue;

				construct_policy(policy_name, _pol + i, Affinity::Location());

				fn(_threads[i], _names[i], *_pol[i]);
				break;
			}
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
