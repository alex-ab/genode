/*
 * \brief  File system for providing a value as a file
 * \author Josef Soentgen
 * \author Sebastian Sumpf
 * \author Norman Feske
 * \date   2018-11-24
 */

/*
 * Copyright (C) 2018-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _VALUE_FILE_SYSTEM_H_
#define _VALUE_FILE_SYSTEM_H_

/* Genode includes */
#include <vfs/single_file_system.h>

namespace Genode::Vfs {
	template <typename, unsigned BUF_SIZE = 64>
	class Value_file_system;
}


template <typename T, unsigned BUF_SIZE>
class Genode::Vfs::Value_file_system : public Single_file_system
{
	public:

		using Name = String<64>;

	private:

		struct Buffer { char bytes[BUF_SIZE]; size_t len; };

		/*
		 * The '_back' buffer is targeted by read and write operations.
		 * The '_front' buffer is consumed by the VFS plugin. In contrast
		 * to '_back' that can be filled via multiple consecutivy write
		 * operations, '_front' contains a consistent state.
		 */
		Buffer _back { }, _front { };

		struct File_channel : Vfs::File_channel
		{
			Allocator         &_alloc;
			Value_file_system &_value_fs;

			File_channel(Allocator &alloc, Attr attr, Value_file_system &value_fs)
			:
				Vfs::File_channel(attr), _alloc(alloc), _value_fs(value_fs)
			{ }

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (at.pos >= _value_fs._back.len)
					return Read_eof();

				size_t const n = min(size_t(_value_fs._back.len - at.pos), dst.num_bytes);

				memcpy(dst.start, _value_fs._back.bytes + at.pos, n);
				return n;
			}

			Write_result write(At const at, Const_byte_range_ptr const &src) override
			{
				if (!writeable || at.pos >= BUF_SIZE)
					return Write_error::DENIED;

				size_t const n = min(size_t(BUF_SIZE - at.pos), src.num_bytes);

				memcpy(_value_fs._back.bytes + at.pos, src.start, n);
				_value_fs._back.len = max(_value_fs._back.len, size_t(at.pos + n));
				return n;
			}

			Resize_result resize(file_size size) override
			{
				if (!writeable || size > BUF_SIZE)
					return Resize_result::DENIED;

				_value_fs._back.len = size_t(size);

				return Resize_result::OK;
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }

			void destruct() override { destroy(_alloc, this); }

			Sync_result sync() override
			{
				_value_fs._front = _value_fs._back;
				_value_fs._notify_watchers();
				return Sync_result::OK;
			}
		};

	public:

		Value_file_system(Parent_fs &parent_fs, Name const &name,
		                  auto const &initial_value)
		:
			Single_file_system(parent_fs, {
				.ident = { { "value ", name } },
				.name  = name,
				.rwx   = File::RW_CONTINUOUS
			})
		{
			value(initial_value);
		}

		void value(auto const &value)
		{
			String<BUF_SIZE + 1>(value).with_span([&] (Span const &s) {
				memcpy(_back.bytes, s.start, s.num_bytes);
				_back.len = s.num_bytes;
			});
			_front = _back;
		}

		T value() const
		{
			T val { };
			parse(Span(_front.bytes, _front.len), val);
			return val;
		}

		void with_span(auto const &fn) const { fn(Span(_front.bytes, _front.len)); }

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
			Stat_result result = Single_file_system::stat(path, out);
			out.size = _back.len;
			return result;
		}
};

#endif /* _VALUE_FILE_SYSTEM_H_ */
