/*
 * \brief  Simple static caching allocator
 * \author Alexander Boettcher
 * \date   2024-03-04
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#pragma once

#include <base/log.h>

template <bool auto_wipe = true, unsigned wipe_unused = 10'000>
class Simple_cache
{
	private:

		#define SIMPLE_MEM_STATISTICS_TRACKING 1

		enum { TYPED_CACHES = 32, BUCKET_COUNT = 16 };

		typedef unsigned long uint64;

		struct Access { uint64 count; };

		struct Bucket {
			void * ptr [BUCKET_COUNT];
			Access last;
			uint64 bucket_size;

			#if SIMPLE_MEM_STATISTICS_TRACKING
			/* statistics, not required for operation */
			uint64 avail;
			uint64 hits;
			#endif

			void for_one_used_entry(auto const align, auto const &fn)
			{
				for (auto &data_ptr : ptr) {
					if (!data_ptr)
						continue;

					if (align && (uint64(data_ptr) % align))
						continue;

					fn(data_ptr);

					return;
				}
			}

			bool for_one_unused_entry(auto const &fn)
			{
				for (auto &data_ptr : ptr) {
					if (data_ptr)
						continue;

					fn(data_ptr);

					return true;
				}

				return false;
			}

		};

		Bucket bucket_typed [TYPED_CACHES] { };

		Access invocations { };

		#if SIMPLE_MEM_STATISTICS_TRACKING
		/* statistics about not supported _free_ calls */
		Access ignored [8192] { };
		#endif

		void loop_buckets(auto const size, auto const &fn)
		{
			unsigned constexpr invalid_unused = ~0U;
			unsigned unused = invalid_unused;

			for (unsigned i = 0; i < TYPED_CACHES; i++) {
				auto &bucket = bucket_typed[i];

				if (unused == invalid_unused && !bucket.bucket_size)
					unused = i;

				if (bucket.bucket_size != size)
					continue;

				if (fn(bucket.bucket_size, bucket, bucket.last))
					return;
			}

			if (unused != invalid_unused) {
				auto &bucket = bucket_typed[unused];

				fn(bucket.bucket_size, bucket, bucket.last);
			}
		}

		void _for_each_bucket(auto const &fn)
		{
			for (auto & bucket : bucket_typed)
				fn(bucket.bucket_size, bucket, bucket.last);
		}

		void * _try_alloc(Bucket &bucket, uint64 const size, uint64 const align)
		{
			void * ptr = nullptr;

			bucket.for_one_used_entry(align, [&](auto &data_ptr) {

				ptr      = data_ptr;
				data_ptr = nullptr;

				Genode::memset(ptr, 0, size);

				#if SIMPLE_MEM_STATISTICS_TRACKING
				bucket.avail --;
				bucket.hits ++;
				#endif
			});

			return ptr;
		}

		bool _cache_free(Bucket &bucket, void const * ptr)
		{
			return bucket.for_one_unused_entry([&](auto &data_ptr) {

				data_ptr = (void *)ptr;

				#if SIMPLE_MEM_STATISTICS_TRACKING
				bucket.avail ++;
				#endif
			});
		}

		bool _wipe_cache(Access const &last) const
		{
			auto const diff = (invocations.count >= last.count)
			                ? invocations.count - last.count
			                : 0ULL - last.count + invocations.count;

			return diff >= wipe_unused;
		}

		void _wipe_check(auto const &fn_free)
		{
			_for_each_bucket([&](auto &bucket_size, auto &bucket, auto &access) {

				if (!_wipe_cache(access))
					return;

				bucket_size = 0;
				access      = invocations;

				for (auto &data_ptr : bucket.ptr) {

					if (!data_ptr)
						continue;

					fn_free(data_ptr);
					data_ptr = nullptr;

					#if SIMPLE_MEM_STATISTICS_TRACKING
					bucket.avail --;
					#endif
				}

				#if SIMPLE_MEM_STATISTICS_TRACKING
				bucket.hits        = 0;
				#endif
			});
		}

	public:

		void * alloc(uint64 const size, uint64 const align)
		{
			void * ptr = nullptr;

			invocations.count ++;

			if (invocations.count % 200'000 == 0)
				dump_stats();

			if (!size)
				return ptr;

			loop_buckets(size, [&](auto &bucket_size, auto &bucket, auto &access) {

				/* skip unused bucket */
				if (!bucket_size)
					return false;

				access = invocations;
				ptr    = _try_alloc(bucket, size, align);

				if (!ptr) {
					/* bucket of right element size has no entries -> unused */
					bucket_size = 0;
				}

				return !!ptr;
			});

			return ptr;
		}


		bool free(void const * ptr, uint64 const size, auto const &fn_free)
		{
			bool freed = false;

			if (auto_wipe)
				_wipe_check(fn_free);

			if (!ptr)
				return false;

			loop_buckets(size, [&](auto &bucket_size, auto &bucket, auto &access) {

				/* claim unused bucket for this size */
				if (!bucket_size)
					bucket_size = size;

				access = invocations;
				freed  = _cache_free(bucket, ptr);

				return freed;
			});

			return freed;
		}

		void dump_stats(bool reset = true)
		{
			#if SIMPLE_MEM_STATISTICS_TRACKING
			unsigned i = 0;
			bool first_uncached = true;

			for (auto &entry : ignored) {
				if (entry.count) {
					if (first_uncached) {
						first_uncached = false;
						Genode::log("ignored :");
					}

					Genode::log(i <   10 ? " " : "",
					            i <  100 ? " " : "",
					            i < 1000 ? " " : "",
					            " ", i, " ", entry.count);
				}

				i ++;

				if (reset)
					entry.count = 0;
			}

			Genode::log("cached :");

			for (auto &bucket : bucket_typed) {
				auto const  data_size = bucket.bucket_size;

				if (data_size)
					Genode::log(data_size <   10 ? " " : "",
					            data_size <  100 ? " " : "",
					            data_size < 1000 ? " " : "",
					            " ", data_size,
					            " avail=", bucket.avail,
					            " hits=", bucket.hits);

				if (reset) {
					bucket.hits        = 0;
				}
			}
			#else
			(void)reset;
			#endif
		}
};
