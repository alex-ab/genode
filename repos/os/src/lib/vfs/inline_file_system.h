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

		Allocator &_alloc;

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

		Constructible<Buffered_node> _node { };
		Constructible<Buffered_data> _data { };

		struct File_channel : Vfs::File_channel
		{
			Allocator &_alloc;
			File_system const &_fs;

			File_channel(Allocator &alloc, File_system const &fs)
			:
				Vfs::File_channel({ .writeable = false }), _alloc(alloc), _fs(fs)
			{ }

			inline Read_result read(At, Byte_range_ptr const &) override;

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return false; }

			void destruct() override { destroy(_alloc, this); }
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
			Single_file_system(parent_fs, {
				.ident = Ident::from_node(config),
				.name  = File::Name::from_node(config),
				.rwx = {
					.r = File::Read::ANYWHERE,
					.w = File::Write::DENIED,
					.x = true
				}
			}),
			_alloc(env.alloc())
		{ }

		Progress update(Node const &node, Factory &) override
		{
			Progress result { };
			if (!_node.constructed() || _node->differs_from(node)) {
				_node.construct(_alloc, node);

				if (node.num_bytes()) _data.construct(_alloc, node);
				else                  _data.destruct();

				result = PROGRESSED;
			}
			return result;
		}

		Open_result open(char const *path, Open_attr, Allocator &alloc) override
		{
			if (!_single_file(path))
				return Open_error::DENIED;

			try { return *new (alloc) File_channel(alloc, *this); }
			catch (Out_of_ram)  { return Open_error::OUT_OF_RAM; }
			catch (Out_of_caps) { return Open_error::OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			Stat_result const result = Single_file_system::stat(path, out);

			if (_data.constructed())
				out.size = _data->num_bytes.convert<size_t>(
					[] (size_t n)     { return n; },
					[] (Buffer_error) { return 0ul; });

			return result;
		}

		static constexpr auto BUILTIN_FS_TYPE = "inline";
};


Genode::Vfs::File_channel::Read_result
Vfs_inline::File_system::File_channel::read(At const at, Byte_range_ptr const &dst)
{
	Read_result result = Read_eof();

	/* file read limit is the size of the node content */
	if (_fs._data.constructed())
		_fs._data->with_bytes([&] (char const *start, size_t const num_bytes) {

			/* maximum read position, clamped to dataspace size */
			size_t const end_pos = min(size_t(dst.num_bytes + at.pos), num_bytes);

			/* check if end of file is reached */
			if (at.pos >= end_pos)
				return;

			size_t const n = size_t(end_pos - at.pos);

			/* copy-out bytes from ROM dataspace */
			memcpy(dst.start, start + at.pos, n);
			result = n;
		});

	return result;
}

#endif /* _INCLUDE__VFS__INLINE_FILE_SYSTEM_H_ */
