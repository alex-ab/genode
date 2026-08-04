/*
 * \brief  File system for providing a read-only value as a file
 * \author Norman Feske
 * \date   2018-03-27
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__READONLY_VALUE_FILE_SYSTEM_H_
#define _INCLUDE__VFS__READONLY_VALUE_FILE_SYSTEM_H_

/* Genode includes */
#include <vfs/single_file_system.h>

namespace Genode::Vfs {
	template <typename, unsigned BUF_SIZE = 128>
	class Readonly_value_file_system;
}


template <typename T, unsigned BUF_SIZE>
class Genode::Vfs::Readonly_value_file_system : public Single_file_system
{
	public:

		using Name = String<64>;

	private:

		using Buffer = String<BUF_SIZE + 1>;

		Name const _file_name;

		Buffer _buffer { };

		struct Vfs_handle : Single_vfs_handle
		{
			Buffer const &_buffer;

			Vfs_handle(Directory_service &ds,
			           Allocator         &alloc,
			           Buffer      const &buffer)
			:
				Single_vfs_handle(ds, alloc, 0), _buffer(buffer)
			{ }

			Read_result read(Byte_range_ptr const &dst) override
			{
				if (seek() > _buffer.length())
					return Read_error::DENIED;

				char const * const src = _buffer.string() + seek();
				size_t const len = min(size_t(_buffer.length() - seek()), dst.num_bytes);
				memcpy(dst.start, src, len);

				return len;
			}

			Write_result write(Const_byte_range_ptr const &, size_t &) override
			{
				return WRITE_ERR_IO;
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return false; }
		};

		using Config = String<200>;
		Config _config(Name const &name) const
		{
			char buf[Config::capacity()] { };
			Generator::generate({ buf, sizeof(buf) }, type_name(),
				[&] (Generator &g) { g.attribute("name", name); }
			).with_error([&] (Buffer_error) {
				warning("VFS read-only value fs config failed (", _file_name, ")");
			});
			return Config(Cstring(buf));
		}

	public:

		Readonly_value_file_system(Parent_fs &parent_fs, Name const &name,
		                           T const &initial_value)
		:
			Single_file_system(parent_fs,
			                   Node_type::TRANSACTIONAL_FILE, type(),
			                   Node_rwx::ro(), Node(_config(name))),
			_file_name(name)
		{
			value(initial_value);
		}

		static char const *type_name() { return "readonly_value"; }

		char const *type() override { return type_name(); }

		void value(T const &value)
		{
			Buffer const orig_buffer = _buffer;

			_buffer = Buffer(value);

			if (_buffer != orig_buffer)
				Single_file_system::_notify_watchers();
		}

		bool matches(Node const &node) const
		{
			return node.has_type(type_name()) &&
			       node.attribute_value("name", Name()) == _file_name;
		}


		/*********************************
		 ** Directory-service interface **
		 *********************************/

		Open_result open(char const *path, unsigned, Vfs::Vfs_handle **out_handle,
		                 Allocator &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				*out_handle = new (alloc)
					Vfs_handle(*this, alloc, _buffer);

				return OPEN_OK;
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			Stat_result result = Single_file_system::stat(path, out);
			out.size = _buffer.length();
			return result;
		}

		using Single_file_system::close;
};

#endif /* _INCLUDE__VFS__READONLY_VALUE_FILE_SYSTEM_H_ */
