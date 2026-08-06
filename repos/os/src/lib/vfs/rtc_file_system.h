/*
 * \brief  Rtc file system
 * \author Josef Soentgen
 * \date   2014-08-20
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__RTC_FILE_SYSTEM_H_
#define _INCLUDE__VFS__RTC_FILE_SYSTEM_H_

/* Genode includes */
#include <base/registry.h>
#include <util/formatted_output.h>
#include <rtc_session/connection.h>
#include <vfs/file_system.h>


namespace Vfs_rtc {

	using namespace Genode;
	using namespace Genode::Vfs;

	class File_system;
}


class Vfs_rtc::File_system : public Single_file_system
{
	private:

		/* "1970-01-01 00:00:00\n" */
		enum { TIMESTAMP_LEN = 20 };

		class Rtc_vfs_handle : public Single_vfs_handle
		{
			private:

				Rtc::Connection &_rtc;

			public:

				Rtc_vfs_handle(Directory_service &ds,
				               Allocator &alloc, Rtc::Connection &rtc)
				:
					Single_vfs_handle(ds, alloc, 0), _rtc(rtc)
				{ }

				/**
				 * Read the current time from the Rtc session
				 *
				 * On each read the current time is queried and afterwards formated
				 * as '%Y-%m-%d %H:%M:%S\n' resp. '%F %T\n'.
				 */
				Read_result read(Byte_range_ptr const &dst) override
				{
					if (seek() >= TIMESTAMP_LEN)
						return 0; /* EOF */

					Rtc::Timestamp ts = _rtc.current_time();

					struct Padded
					{
						unsigned pad, value;

						void print(Output &out) const
						{
							using namespace Genode;

							unsigned const len = printed_length(value);
							if (len < pad)
								Genode::print(out, Repeated(pad - len, Char('0')));

							Genode::print(out, value);
						}
					};

					String<TIMESTAMP_LEN+1> string { Padded { 4, ts.year   }, "-",
					                                 Padded { 2, ts.month  }, "-",
					                                 Padded { 2, ts.day    }, " ",
					                                 Padded { 2, ts.hour   }, ":",
					                                 Padded { 2, ts.minute }, ":",
					                                 Padded { 2, ts.second }, "\n" };
					char const *b = string.string();
					size_t      n = string.length();

					n -= size_t(seek());
					b += seek();

					size_t const len = min(n, dst.num_bytes);
					memcpy(dst.start, b, len);
					return len;
				}

				bool read_ready()  const override { return true; }
				bool write_ready() const override { return false; }
		};

		Rtc::Connection _rtc;

		Io_signal_handler<File_system> _set_signal_handler;

		void _handle_set_signal() { Single_file_system::_notify_watchers(); }

	public:

		File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &config)
		:
			Single_file_system(parent_fs,
			                   Node_type::TRANSACTIONAL_FILE, name(),
			                   Node_rwx::ro(), config),
			_rtc(env.env()),
			_set_signal_handler(env.env().ep(), *this,
			                    &File_system::_handle_set_signal)
		{
			_rtc.set_sigh(_set_signal_handler);
		}

		static char const *name()   { return "rtc"; }
		char const *type() override { return "rtc"; }

		/*********************************
		 ** Directory-service interface **
		 *********************************/

		Open_result open(char const  *path, unsigned,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				*out_handle = new (alloc)
					Rtc_vfs_handle(*this, alloc, _rtc);
				return OPEN_OK;
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			Stat_result result = Single_file_system::stat(path, out);

			if (result == STAT_OK) {
				out.size = TIMESTAMP_LEN;
			}

			return result;
		}
};

#endif /* _INCLUDE__VFS__RTC_FILE_SYSTEM_H_ */
