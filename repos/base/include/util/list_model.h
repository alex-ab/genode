/*
 * \brief  List-based data model created and updated from node content
 * \author Norman Feske
 * \date   2017-08-09
 *
 * The 'List_model' stores a component-internal representation of node
 * content. The internal representation 'ELEM' carries two methods 'matches'
 * and 'type_matches' that define the relation of the elements to nodes.
 * E.g.,
 *
 * ! struct Item : List_model<Item>::Element
 * ! {
 * !   static bool type_matches(Node const &);
 * !
 * !   bool matches(Node const &) const;
 * !   ...
 * ! };
 *
 * The class function 'type_matches' returns true if the specified 'Node'
 * matches the 'Item' type. It can thereby be used to control the creation of
 * 'ELEM' nodes by responding to specific node types while ignoring unrelated
 * nodes.
 *
 * The 'matches' method returns true if the concrete element instance matches
 * the given node. It is used to correlate existing 'ELEM' objects with new
 * versions of nodes to update the 'ELEM' objects.
 *
 * The functor arguments 'create_fn', 'destroy_fn', and 'update_fn' for the
 * 'update' method define how objects are created, destructed, and updated.
 * E.g.,
 *
 * ! _list_model.update_from_node(node,
 * !
 * !   [&] (Node const &node) -> Item & {
 * !     return *new (alloc) Item(node); },
 * !
 * !   [&] (Item &item) { destroy(alloc, &item); },
 * !
 * !   [&] (Item &item, Node const &node) { item.update(node); }
 * ! );
 *
 * The elements are ordered according to the order of sub nodes.
 *
 * The list model is a container owning the elements. Before destructing a
 * list model, its elements must be removed by calling 'update' with an empty
 * 'Node()' as argument, which results in the call of 'destroy_fn' for each
 * element.
 *
 * In cases where the construction of an item may fail, the model update can
 * be canceled by letting the 'create_fn' functor return 'Error::CANCELED' of
 * type 'List_model<ELEM>::Create_result', e.g.,
 *
 * !   [&] (Node const &node) -> List_model<Item>::Create_result {
 * !     ...
 * !     return List_model<Item>::Error::CANCELED;
 * !   }
 *
 * Note that a cancelled model update leaves the model in a state that is not
 * consistent with the node argument. Consistency is re-established once
 * another call of 'update_from_node' completes without cancellation.
 */

/*
 * Copyright (C) 2017-2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__UTIL__LIST_MODEL_H_
#define _INCLUDE__UTIL__LIST_MODEL_H_

/* Genode includes */
#include <util/noncopyable.h>
#include <util/list.h>
#include <base/log.h>
#include <base/node.h>

namespace Genode { template <typename> class List_model; }


template <typename ELEM>
class Genode::List_model : Noncopyable
{
	private:

		List<ELEM> _elements { };

	public:

		enum class Error { CANCELED };
		using Create_result = Unique_attempt<ELEM &, Error>;

		struct Update_policy;

		class Element : private List<ELEM>::Element
		{
			private:

				/* used by 'List_model::update_from_node' only */
				friend class List_model;
				friend class List<ELEM>;

				ELEM *_next() const { return List<ELEM>::Element::next(); }

				/**
				 * Noncopyable
				 */
				Element(Element const &) = delete;
				Element & operator = (Element const &) = delete;

			public:

				Element() { };

				/**
				 * Return the element's neighbor if present, otherwise nullptr
				 */
				ELEM const *next() const { return List<ELEM>::Element::next(); }
		};

		~List_model()
		{
			if (_elements.first())
				warning("list model not empty at destruction time");
		}

		/**
		 * Update data model according to the given node
		 */
		inline void update_from_node(auto const &node,
		                             auto const &create_fn,
		                             auto const &destroy_fn,
		                             auto const &update_fn);

		/**
		 * API-compatibility wrapper for 'update_from_node'
		 *
		 * \noapi
		 * \deprecated
		 */
		inline void update_from_xml(auto &&... args) { update_from_node(args...); }

		/**
		 * Call functor 'fn' for each const element
		 */
		void for_each(auto const &fn) const
		{
			for (Element const *e = _elements.first(); e; e = e->next())
				fn(static_cast<ELEM const &>(*e));
		}

		/**
		 * Call functor 'fn' for each non-const element
		 */
		void for_each(auto const &fn)
		{
			Element *next = nullptr;
			for (Element *e = _elements.first(); e; e = next) {
				next = e->_next();
				fn(static_cast<ELEM &>(*e));
			}
		}

		/**
		 * Call functor 'fn' with the first element of the list model
		 *
		 * Using this method combined with 'Element::next', the list model
		 * can be traversed manually. This is handy in situations where the
		 * list-model elements are visited via recursive function calls
		 * instead of a 'for_each' loop.
		 */
		void with_first(auto const &fn) const
		{
			if (Element const *e = _elements.first())
				fn(static_cast<ELEM const &>(*e));
		}
};


template <typename ELEM>
void Genode::List_model<ELEM>::update_from_node(auto const &node,
                                                auto const &create_fn,
                                                auto const &destroy_fn,
                                                auto const &update_fn)
{
	List<ELEM> updated_list;

	ELEM *last_updated = nullptr; /* used for appending to 'updated_list' */

	struct Guard
	{
		List<ELEM> &_elements;
		List<ELEM> &_updated_list;
		decltype(destroy_fn) const &_destroy_fn;
		bool complete_without_exception = false;

		~Guard()
		{
			if (!complete_without_exception) {
				/*
				 * Cancel the update when an 'Error::CANCELED' or an exception
				 * has occurred during 'create_fn'. Retain the unvisited
				 * elements but leave the remaining model out of date.
				 */
				while (ELEM *e = _elements.first()) {
					_elements.remove(e);
					_updated_list.insert(e);
				}
			}

			/* remove stale elements */
			ELEM *next = nullptr;
			for (ELEM *e = _elements.first(); e; e = next) {
				next = e->_next();
				_destroy_fn(*e);
			}

			/* use 'updated_list' list as new data model */
			_elements = _updated_list;
		}
	} guard {
		._elements     = _elements,
		._updated_list = updated_list,
		._destroy_fn   = destroy_fn
	};

	bool canceled = false;

	node.for_each_sub_node([&] (auto const &sub_node) {

		if (canceled)
			return;

		/* skip nodes that are unrelated to the data model */
		if (!ELEM::type_matches(sub_node))
			return;

		/* check for duplicates, which must not exist in the list model */
		for (ELEM *dup = updated_list.first(); dup; dup = dup->_next()) {

			/* update existing element with information from later node */
			if (dup->matches(sub_node)) {
				update_fn(*dup, sub_node);
				return;
			}
		}

		/* look up corresponding element in original list */
		ELEM *curr = _elements.first();
		while (curr && !curr->matches(sub_node))
			curr = curr->_next();

		/* consume existing element or create new one */
		if (curr) {
			_elements.remove(curr);
		} else {
			Create_result result = create_fn(sub_node);
			result.with_result([&] (ELEM &e) { curr = &e; },
			                   [&] (Error)   { canceled = true; });
		}

		if (canceled)
			return;

		/* append current element to 'updated_list' */
		updated_list.insert(curr, last_updated);
		last_updated = curr;

		update_fn(*curr, sub_node);
	});

	if (!canceled)
		guard.complete_without_exception = true;
}

#endif /* _INCLUDE__UTIL__LIST_MODEL_H_ */
