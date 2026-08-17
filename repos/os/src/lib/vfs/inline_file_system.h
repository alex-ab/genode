/*
 * \brief  Inline filesystem
 * \author Norman Feske
 * \date   2014-04-14
 *
 * This file system allows the content of a file being specified as the content
 * of its config node.
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__INLINE_FILE_SYSTEM_H_
#define _INCLUDE__VFS__INLINE_FILE_SYSTEM_H_

#include <vfs/file_system.h>

namespace Vfs_inline {

	using namespace Genode;
	using namespace Genode::Vfs;

	class File_system;
}


class Vfs_inline::File_system : public Single_file_system
{
	private:

		struct Buffered_data
		{
			using Allocated = Memory::Constrained_allocator::Result;

			Allocated allocated;

			using Num_bytes = Attempt<size_t, Buffer_error>;

			Num_bytes num_bytes = Buffer_error::EXCEEDED;  /* unquoted data size */

			static bool _has_quoted_content(Node const &node)
			{
				bool result = false;
				node.for_each_quoted_line([&] (auto const &) { result = true; });
				return result;
			}

			static Num_bytes unquoted_content(Byte_range_ptr const &dst, auto const &node)
			{
				using namespace Genode;

				if (_has_quoted_content(node))
					return dst.as_output([&] (Output &output) {
						print(output, Node::Quoted_content(node)); });

				if (node.num_sub_nodes() != 1) {
					warning("exactly one sub node expected: ", node);
					return 0ul;
				}

				return node.with_sub_node(0u, [&] (auto const &content) {
					return Generator::generate(dst, content.type(),
						[&] (Generator &g) {
							g.node_attributes(content);
							if (!g.append_node_content(content, { 20 }))
								warning("inline fs too deeply nested: ", content);
						});
				}, [&] () -> Num_bytes { /* checked above */ return 0ul; });
			}

			static Num_bytes _copy_from_node(Allocated &allocated, Node const &node)
			{
				return allocated.convert<Num_bytes>([&] (Memory::Allocation &a) {
					return unquoted_content({ (char *)a.ptr, a.num_bytes }, node);
				},
				[&] (Alloc_error) {
					warning("inline VFS allocation failed");
					return 0ul;
				});
			}

			void with_bytes(auto const &fn) const
			{
				using namespace Genode;

				num_bytes.with_result(
					[&] (size_t n) {
						if (n)
							allocated.with_result([&] (Memory::Allocation const &a) {
								fn((char const *)a.ptr, n); }, [&] (auto) { });
					},
					[&] (Buffer_error) { warning("inline VFS decoding failed"); });
			}

			Buffered_data(Memory::Constrained_allocator &alloc,
			              Node const &node)
			:
				/* use node size as upper approximation of data size */
				allocated(alloc.try_alloc(node.num_bytes())),
				num_bytes(_copy_from_node(allocated, node))
			{ }
		};

		Buffered_data const _data;

		class Handle : public Single_vfs_handle
		{
			private:

				File_system const &_fs;

			public:

				Handle(Directory_service &ds,
				       Allocator         &alloc,
				       File_system const &inline_fs)
				:
					Single_vfs_handle(ds, alloc, 0), _fs(inline_fs)
				{ }

				inline Read_result read(At, Byte_range_ptr const &) override;

				bool read_ready()  const override { return true; }
				bool write_ready() const override { return false; }
		};

	public:

		/**
		 * Constructor
		 *
		 * The 'config' node (that points to its content) is stored within
		 * the object after construction time. The underlying backing store
		 * must be kept in tact during the lifefile of the object.
		 */
		File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &config)
		:
			Single_file_system(parent_fs,
			                   Node_type::CONTINUOUS_FILE, name(),
			                   Node_rwx::rx(), config),
			_data(env.alloc(), config)
		{ }

		static char const *name()   { return "inline"; }
		char const *type() override { return "inline"; }

		/********************************
		 ** Directory service interface **
		 ********************************/

		Open_result open(char const  *path, unsigned,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				*out_handle = new (alloc) Handle(*this, alloc, *this);
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }

			return OPEN_OK;
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			Stat_result const result = Single_file_system::stat(path, out);

			out.size = _data.num_bytes.convert<size_t>(
				[] (size_t n)     { return n; },
				[] (Buffer_error) { return 0ul; });

			return result;
		}
};


Genode::Vfs::Vfs_handle::Read_result
Vfs_inline::File_system::Handle::read(At const at, Byte_range_ptr const &dst)
{
	Read_result result = Read_eof();

	/* file read limit is the size of the node content */
	_fs._data.with_bytes([&] (char const *data_start, size_t const data_num_bytes) {

		/* maximum read position, clamped to dataspace size */
		size_t const end_pos = min(size_t(dst.num_bytes + at.pos), data_num_bytes);

		/* check if end of file is reached */
		if (at.pos >= end_pos)
			return;

		size_t const n = size_t(end_pos - at.pos);

		/* copy-out bytes from ROM dataspace */
		memcpy(dst.start, data_start + at.pos, n);
		result = n;
	});
	return result;
}

#endif /* _INCLUDE__VFS__INLINE_FILE_SYSTEM_H_ */
