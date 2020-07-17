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
#include <base/rpc_server.h>
#include <base/trace/types.h>
#include <cpu_session/client.h>
#include <os/reporter.h>

namespace Cpu {
	using namespace Genode;

	using Genode::Trace::Subject_id;

	class Session;
	class Trace;
	typedef Id_space<Parent::Client>::Element Client_id;
	typedef List<List_element<Session> > Child_list;
}

class Cpu::Session : public Rpc_object<Cpu_session>
{
	private:

		Env                   &_env;
		Parent::Client         _parent_client { };
		Client_id const        _id            { _parent_client,
		                                        _env.id_space() };
		Cpu_session_client     _parent;

		Child_list            &_list;
		List_element<Session>  _list_element { this };

		Session::Label         _label;
		Affinity         const _affinity;

		enum { MAX_THREADS = 32 };

		Thread_capability      _threads    [MAX_THREADS];
		Affinity::Location     _location   [MAX_THREADS];
		Thread::Name           _names      [MAX_THREADS];
		Subject_id             _subject_id [MAX_THREADS];
		uint8_t                _policy     [MAX_THREADS];

		bool                   _report { true };

		enum {
			POLICY_NONE = 0,
			POLICY_PIN,
			POLICY_ROUND_ROBIN,
			POLICY_MAX_UTILIZE,
		};

		template <typename FUNC>
		void apply(FUNC const &fn)
		{
			for (unsigned i = 0; i < MAX_THREADS; i++) {
				if (!_threads[i].valid())
					continue;

				bool const done = fn(_threads[i], _location[i], _names[i],
				                     _policy[i], _subject_id[i]);
				if (done)
					break;
			}
		}

		template <typename FUNC>
		void lookup(Thread::Name const &name, FUNC const &fn)
		{
			for (unsigned i = 0; i < MAX_THREADS; i++) {
				if (_names[i] != name)
					continue;

				if (fn(_threads[i], _location[i], _policy[i]))
					break;
			}
		}

		template <typename FUNC>
		void construct(FUNC const &fn)
		{
			for (unsigned i = 0; i < MAX_THREADS; i++) {
				if (_threads[i].valid() || _names[i].valid())
					continue;

				if (fn(_threads[i], _location[i], _names[i], _policy[i]))
					break;
			}
		}

		void _schedule(Thread_capability const &, Affinity::Location &,
		               Affinity::Location &, Name const &, uint8_t const &);

	public:

		/**
		 * Constructor
		 */
		Session(Env &, Affinity const &,
		        Root::Session_args const &, Child_list &);

		~Session();


		/***************************
		 ** CPU session interface **
		 ***************************/

		Thread_capability create_thread(Pd_session_capability, Name const &,
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
		void config(Name const &, Name const &, Affinity::Location const &);
		void iterate_threads();
		void iterate_threads(Trace &);
		void report_state(Xml_generator &);
		bool report_update() const { return _report; }
};

#endif /* _SESSION_H_ */
