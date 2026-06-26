/*
 * \brief  Interface for obtaining real-time clock values
 * \author Norman Feske
 * \date   2020-08-18
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC__INTERNAL__RTC_H_
#define _LIBC__INTERNAL__RTC_H_

#include <sys/time.h>

namespace Libc { struct Rtc; }


struct Libc::Rtc
{
	using Allocator = Genode::Allocator;

	Directory &_root_dir;

	Allocator &_alloc;

	Rtc_path const _rtc_path;

	time_t _rtc_value { 0 };

	bool _rtc_value_out_of_date { true };

	Milliseconds _msecs_when_rtc_updated { 0 };

	bool const _rtc_path_valid = (_rtc_path != "");

	Constructible<Io::Watch_handler<Rtc>> _watch_io_handler { };

	void _handle_watch() { _rtc_value_out_of_date = true; }

	void _update_rtc_value_from_file()
	{
		try {
			File_content const content(_alloc, _root_dir, _rtc_path.string(),
			                           File_content::Limit{4096U});
			content.bytes([&] (char const *ptr, size_t size) {

				char buf[32] { };
				::memcpy(buf, ptr, min(sizeof(buf) - 1, size));

				struct tm tm { };
				if (strptime(buf, "%Y-%m-%d %H:%M:%S", &tm)
				 || strptime(buf, "%Y-%m-%d %H:%M", &tm)) {
					_rtc_value = timegm(&tm);
					if (_rtc_value == (time_t)-1)
						_rtc_value = 0;
				}
			});
		} catch (...) {
			warning(_rtc_path, " not readable, returning ", _rtc_value);
		}
	}

	Rtc(Directory &root_dir, Allocator &alloc, Rtc_path const &rtc_path)
	:
		_root_dir(root_dir), _alloc(alloc), _rtc_path(rtc_path)
	{
		if (!_rtc_path_valid) {
			warning("rtc not configured, returning ", _rtc_value);
			return;
		}

		_watch_io_handler.construct(_root_dir, _rtc_path, *this, &Rtc::_handle_watch);
	}

	timespec read(Duration current_time)
	{
		struct timespec result { };

		if (!_rtc_path_valid)
			return result;

		if (_rtc_value_out_of_date) {
			_update_rtc_value_from_file();
			_msecs_when_rtc_updated = current_time.trunc_to_plain_ms();
			_rtc_value_out_of_date = false;
		}

		/*
		 * Return time as sum of cached RTC value and relative 'current_time'
		 */
		Milliseconds const current_msecs = current_time.trunc_to_plain_ms();

		Milliseconds const msecs_since_rtc_update {
			current_msecs.value - _msecs_when_rtc_updated.value };

		uint64_t const seconds_since_rtc_update =
			msecs_since_rtc_update.value / 1000;

		result.tv_sec  = _rtc_value + seconds_since_rtc_update;
		result.tv_nsec = (msecs_since_rtc_update.value % 1000) * (1000*1000);

		return result;
	}
};

#endif /* _LIBC__INTERNAL__RTC_H_ */
