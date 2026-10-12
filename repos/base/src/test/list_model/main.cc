/*
 * \brief  Test for the list-model utility
 * \author Norman Feske
 * \date   2026-10-12
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <util/list_model.h>
#include <base/heap.h>
#include <base/id_space.h>
#include <base/log.h>
#include <base/sleep.h>
#include <base/component.h>

using namespace Genode;


static void test_cancel(Env &env)
{
	struct Item;
	using Items = Id_space<Item>;

	struct Item : Items::Element, List_model<Item>::Element
	{
		Item(Items::Id id, Items &items) : Items::Element(*this, items, id) { }

		static bool type_matches(Node const &) { return true; }

		static Items::Id id_from_node(Node const &node)
		{
			return { node.attribute_value("name", 0ul) };
		}

		bool matches(Node const &node) const { return id_from_node(node) == id(); }
	};

	Items items { };
	List_model<Item> list_model { };

	Heap heap { env.ram(), env.rm() };

	auto num_elements = [&]
	{
		unsigned count = 0;
		list_model.for_each([&] (Item const &) { count++; });
		return count;
	};

	auto check_lifetime_consistency = [&]
	{
		unsigned num_ids = 0;
		items.for_each<Item const>([&] (Item const &) { num_ids++; });

		if (num_ids != num_elements()) {
			error("list model not consistent with ID space");
			sleep_forever();
		}
	};

	auto good_update = [&] (char const *s)
	{
		list_model.update_from_node(Node(Span::from_cstring(s)),
			[&] (Node const &node) -> Item & {
				return *new (heap) Item(Item::id_from_node(node), items); },
			[&] (Item &item) { destroy(heap, &item); },
			[&] (Item &, Node const &) { }
		);
		check_lifetime_consistency();
	};

	char const *v1 = "items\n+ item 1\n+ item 2\n+ item 3\n-";
	good_update(v1);
	log(num_elements(), " after first update");

	auto bad_update_with_exception = [&] (char const *s)
	{
		try {
			list_model.update_from_node(Node(Span::from_cstring(s)),
				[&] (Node const &node) -> Item & {
					log("exception while creating item for ", node);
					throw 1;
				},
				[&] (Item &item) { destroy(heap, &item); },
				[&] (Item &, Node const &) { }
			);
		} catch (...) { };
		check_lifetime_consistency();
	};

	char const *v2 = "items\n+ item 1\n+ item 4\n+ item 3\n-";
	bad_update_with_exception(v2);
	log(num_elements(), " after second (failed) update");
	good_update(v2);
	log(num_elements(), " after second (recovered) update");

	auto bad_update_with_cancelation = [&] (char const *s)
	{
		list_model.update_from_node(Node(Span::from_cstring(s)),
			[&] (Node const &node) -> List_model<Item>::Create_result {
				log("cancel creating item for ", node);
				return List_model<Item>::Error::CANCELED;
			},
			[&] (Item &item) { destroy(heap, &item); },
			[&] (Item &, Node const &) { }
		);
		check_lifetime_consistency();
	};

	char const *v3 = "items\n+ item 1\n+ item 5\n+ item 3\n-";
	bad_update_with_cancelation(v3);
	log(num_elements(), " after third (failed) update");
	good_update(v3);
	log(num_elements(), " after third (recovered) update");

	char const *v4 = "items\n-";
	good_update(v4);
}


void Component::construct(Env &env)
{
	log("--- list-model test ---");

	test_cancel(env);

	log("--- finished list-model test ---");
}
