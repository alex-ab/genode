/*
 * \brief  Tracing structures split up in component and thread
 * \author Alexander Boettcher
 * \date   2019-03-15
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

namespace Top {
	struct Component; struct Thread;
	using namespace Genode::Trace;
	using Genode::Affinity;
}

struct Top::Component : Genode::Avl_string<Genode::Session_label::size()>
{
	Genode::List<Top::Thread> _threads { };
	Component(char const * const name) : Avl_string(name) { }
};

struct Top::Thread
: Genode::List<Top::Thread>::Element, Genode::Avl_node<Top::Thread>
{
	private:

		Top::Component      &_component;
		Thread_name          _thread_name;
		Subject_info::State  _state;
		Policy_id            _policy_id;
		Subject_id     const _id;
		Execution_time       _execution_time { };
		Affinity::Location   _affinity;

		unsigned long long   _recent_execution_time { 0 };
		bool                 _track { false };

	public:

		Thread(Top::Component &component, Subject_id const id,
		       Genode::Trace::Subject_info const info)
		:
			_component(component), _thread_name(info.thread_name()),
			_state(info.state()), _policy_id(info.policy_id()), _id(id),
			_affinity(info.affinity())
		{
			_component._threads.insert(this);
		}

		~Thread()
		{
			_component._threads.remove(this);
		}

		char const *         session_label()  const { return _component.name(); }
		Thread_name const   &thread_name()    const { return _thread_name; }
		Subject_info::State  state()          const { return _state; }
		Policy_id            policy_id()      const { return _policy_id; }
		Execution_time       execution_time() const { return _execution_time; }
		Affinity::Location   affinity()       const { return _affinity; }
		Subject_id           id()             const { return _id; }
		bool                 track()          const { return _track; }
		void                 track(bool track)      { _track = track; }

		bool session_label(const char *compare) const {
			return Genode::strcmp(compare, _component.name()) == 0; }

		unsigned long long   recent_execution_time() const {
			return _recent_execution_time; }

		void update(Genode::Trace::Subject_info const &info)
		{
			if (info.execution_time().value < _execution_time.value)
				_recent_execution_time = 0;
			else
				_recent_execution_time = info.execution_time().value -
			                             _execution_time.value;

			_execution_time = info.execution_time();
			_state          = info.state();
			_policy_id      = info.policy_id();
			_affinity       = info.affinity();
		}

		template <typename FN>
		void for_each_thread_of_pd(FN const &fn) const
		{
			for (Top::Thread *thread = _component._threads.first(); thread; thread = thread->next())
			{
				fn(*thread);
			}
		}

		/*****************
		 * Avl interface *
		 *****************/

		Thread *find_by_id(Genode::Trace::Subject_id const id)
		{
			if (id == _id) return this;
			Thread *obj = this->child(id.id > _id.id);
			return obj ? obj->find_by_id(id) : nullptr;
		}

		bool higher(Thread *e) { return e->_id.id > _id.id; }
};

