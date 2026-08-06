/*
 * \brief  File system for providing a value as a file
 * \author Josef Soentgen
 * \author Sebastian Sumpf
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

		using Buffer = String<BUF_SIZE + 1>;

		Name const _file_name;

		Buffer _buffer { };

		struct Vfs_handle : Single_vfs_handle
		{
			Value_file_system &_value_fs;
			Buffer            &_buffer{ _value_fs._buffer };

			Vfs_handle(Value_file_system &value_fs, Allocator &alloc)
			:
				Single_vfs_handle(value_fs, alloc, 0),
				_value_fs(value_fs)
			{ }

			Read_result read(Byte_range_ptr const &dst) override
			{
				if (seek() > _buffer.length())
					return Read_error::DENIED;

				char const * const src = _buffer.string() + seek();
				size_t const len = min((size_t)(_buffer.length() - seek()), dst.num_bytes);

				memcpy(dst.start, src, len);
				return len;
			}

			Write_result write(Const_byte_range_ptr const &src) override
			{
				if (seek() > BUF_SIZE)
					return Write_error::DENIED;

				size_t const len = min(size_t(BUF_SIZE- seek()), src.num_bytes);

				_buffer = Buffer(Cstring(src.start, len));

				_value_fs._notify_watchers();

				return len;
			}

			Ftruncate_result ftruncate(file_size size) override
			{
				if (size >= BUF_SIZE)
					return FTRUNCATE_ERR_NO_SPACE;

				return FTRUNCATE_OK;
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }

			private:

			Vfs_handle(Vfs_handle const &);
			Vfs_handle &operator = (Vfs_handle const &); 
		};

		using Config = String<200>;
		Config _config(Name const &name) const
		{
			char buf[Config::capacity()] { };
			Generator::generate({ buf, sizeof(buf) }, type_name(),
				[&] (Generator &g) { g.attribute("name", name); }
			).with_error([&] (Buffer_error) {
				warning("VFS value fs config failed (", _file_name, ")");
			});
			return Config(Cstring(buf));
		}

	public:

		Value_file_system(Parent_fs &parent_fs, Name const &name,
		                  Buffer const &initial_value)
		:
			Single_file_system(parent_fs,
			                   Node_type::TRANSACTIONAL_FILE, type(),
			                   Node_rwx::rw(), Node(_config(name))),
			_file_name(name)
		{
			value(initial_value);
		}

		static char const *type_name() { return "value"; }

		char const *type() override { return type_name(); }

		void value(Buffer const &value)
		{
			_buffer = Buffer(value);
		}

		T value()
		{
			T val { 0 };
			ascii_to(_buffer.string(), val);

			return val;
		}

		Buffer buffer() const  { return _buffer; }

		bool matches(Node const &node) const
		{
			return node.has_type(type_name()) &&
			       node.attribute_value("name", Name()) == _file_name;
		}

		Open_result open(char const  *path, unsigned,
		                 Vfs::Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				*out_handle = new (alloc) Vfs_handle(*this, alloc);
				return OPEN_OK;
			}
			catch (Out_of_ram)  { error("out of ram"); return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { error("out of caps");return OPEN_ERR_OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			Stat_result result = Single_file_system::stat(path, out);
			out.size = _buffer.length();
			return result;
		}

		using Single_file_system::close;
};

#endif /* _VALUE_FILE_SYSTEM_H_ */
