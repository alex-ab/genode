/*
 * \brief  Report file system
 * \author Norman Feske
 * \date   2026-10-21
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__REPORT_FILE_SYSTEM_H_
#define _INCLUDE__VFS__REPORT_FILE_SYSTEM_H_

#include <os/reporter.h>
#include <vfs/single_file_system.h>

namespace Vfs_report {
	using namespace Genode;
	using namespace Genode::Vfs;
	struct File_system;
}


struct Vfs_report::File_system : Single_file_system
{
	using Label = String<64>;

	size_t const _limit;

	size_t _size = 0;

	Allocator::Allocation::Attempt _buffer;

	Reporter _reporter;

	struct File_channel : Vfs::File_channel
	{
		Allocator   &_alloc;
		File_system &_fs;

		bool _modified = false;

		File_channel(Allocator &alloc, Attr attr, File_system &fs)
		:
			Vfs::File_channel(attr), _alloc(alloc), _fs(fs)
		{ }

		Read_result read(At at, Byte_range_ptr const &dst) override
		{
			size_t const pos = size_t(at.pos),
			             end = size_t(pos + dst.num_bytes);

			if (pos != at.pos || end < pos)
				return Read_error::DENIED;

			if (pos >= _fs._size)
				return Read_eof();

			size_t const n = end - pos;

			return _fs._buffer.convert<Read_result>(
				[&] (Allocator::Allocation &a) {
					memcpy(dst.start, (char *)a.ptr + pos, n);
					return n;
				},
				[&] (Alloc_error) { return Read_eof(); });
		}

		Write_result write(At at, Const_byte_range_ptr const &src) override
		{
			size_t const pos = size_t(at.pos),
			             end = size_t(pos + src.num_bytes);

			if (!writeable || pos != at.pos || end < pos || pos >= _fs._limit)
				return Write_error::DENIED;

			size_t const n = min(end, _fs._limit) - pos;

			return _fs._buffer.convert<Write_result>(
				[&] (Allocator::Allocation &a) {
					memcpy((char *)a.ptr + pos, src.start, n);
					_modified = true;
					_fs._size = max(_fs._size, pos + n);
					return n;
				},
				[&] (Alloc_error) { return Write_error::DENIED; });
		}

		bool read_ready()  const override { return true; }
		bool write_ready() const override { return true; }

		Sync_result sync() override
		{
			if (_modified) {
				_fs._buffer.with_result(
					[&] (Allocator::Allocation &a) {
						/*
						 * 'Buffer_error' cannot occur because '_buffer' is
						 * bounded by '_limit'
						 */
						(void)_fs._reporter.generate(Span((char const *)a.ptr, _fs._size));
					},
					[&] (Alloc_error) { });
				_modified = false;
			}
			return Sync_result::OK;
		}

		Resize_result resize(file_size size) override
		{
			if (size > _fs._limit)
				return Resize_result::DENIED;

			_fs._size = size_t(size);
			_modified = true;
			return Resize_result::OK;
		}

		void destruct() override { destroy(_alloc, this); }
	};

	File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &config)
	:
		Single_file_system(parent_fs, {
			.ident = Ident::from_node(config),
			.name  = File::Name::from_node(config),
			.rwx   = File::RW_CONTINUOUS
		}),
		_limit(config.attribute_value("limit", Num_bytes { 16*1024 })),
		_buffer(env.alloc().try_alloc(_limit)),
		_reporter(env.env(), "" /* top-level node type is unused */,
		          config.attribute_value("label", Label(name.string)).string(),
		          _limit)
	{
		_reporter.enabled(true);
	}

	Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
	{
		if (!_single_file(path))
			return Open_error::DENIED;

		try {
			return *new (alloc)
				File_channel(alloc, { .writeable = attr.writeable }, *this);
		}
		catch (Out_of_ram)  { return Open_error::OUT_OF_RAM; }
		catch (Out_of_caps) { return Open_error::OUT_OF_CAPS; }
	}

	Stat_result stat(char const *path, Stat &out) override
	{
		Stat_result const result = Single_file_system::stat(path, out);
		if (result == Stat_result::OK) out.size = _size;
		return result;
	}

	static constexpr auto BUILTIN_FS_TYPE = "report";
};

#endif /* _INCLUDE__VFS__REPORT_FILE_SYSTEM_H_ */
