/*
 * \brief  Registry for keeping track of mmapped regions
 * \author Norman Feske
 * \date   2012-08-16
 */

/*
 * Copyright (C) 2012-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__MMAP_REGISTRY_H_
#define _LIBC__INTERNAL__MMAP_REGISTRY_H_

/* Genode includes */
#include <base/mutex.h>
#include <base/env.h>
#include <base/log.h>
#include <libc/allocator.h>

/* libc includes */
#include <errno.h>

/* libc-internal includes */
#include <internal/types.h>

namespace Libc {

	class Mmap_registry;

	/**
	 * Return singleton instance of mmap registry
	 */
	Mmap_registry &mmap_registry();
}


class Libc::Mmap_registry
{
	public:

		struct Attr
		{
			void  *start;
			size_t num_bytes;
			bool   anonymous;
		};

		struct Entry : List<Entry>::Element
		{
			Attr const attr;

			Entry(Attr attr) : attr(attr) { }
		};

	private:

		Libc::Allocator _md_alloc;

		List<Mmap_registry::Entry> _list;

		Pthread_mutex mutable _mutex;

		/*
		 * Common for both const and non-const lookup functions
		 */
		template <typename ENTRY>
		static ENTRY *_lookup_by_addr_unsynchronized(ENTRY *curr, void * const start)
		{
			for (; curr; curr = curr->next())
				if (curr->attr.start == start)
					return curr;

			return 0;
		}

		Entry const *_lookup_by_addr_unsynchronized(void * const start) const
		{
			return _lookup_by_addr_unsynchronized(_list.first(), start);
		}

		Entry *_lookup_by_addr_unsynchronized(void * const start)
		{
			return _lookup_by_addr_unsynchronized(_list.first(), start);
		}

	public:

		void insert(Attr attr)
		{
			Pthread_mutex::Guard guard(_mutex);

			if (_lookup_by_addr_unsynchronized(attr.start)) {
				warning(__func__, ": mmap region at ", attr.start, " "
				        "is already registered");
				return;
			}

			_list.insert(new (&_md_alloc) Entry(attr));
		}

		auto with_registered(void *start, auto const &fn, auto const &missing_fn)
		-> decltype(missing_fn())
		{
			Pthread_mutex::Guard guard(_mutex);

			Entry * const e = _lookup_by_addr_unsynchronized(start);

			auto remove_fn = [&] {
				_list.remove(e);
				destroy(&_md_alloc, e);
			};

			return e ? fn(e->attr, remove_fn) : missing_fn();
		}

		void remove(void *start)
		{
			Pthread_mutex::Guard guard(_mutex);

			Entry *e = _lookup_by_addr_unsynchronized(start);

			if (!e) {
				warning("lookup for address ", start, " "
				        "in in mmap registry failed");
				return;
			}

			_list.remove(e);
			destroy(&_md_alloc, e);
		}
};


#endif /* _LIBC__INTERNAL__MMAP_REGISTRY_H_ */
