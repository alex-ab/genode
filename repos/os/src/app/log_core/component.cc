/*
 * \brief  Component transforming core and kernel output to Genode LOG output
 * \author Alexander Boettcher
 * \date   2016-12-22
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* base includes */
#include <base/component.h>
#include <log_session/connection.h>

/* os includes */
#include <base/attached_rom_dataspace.h>
#include <timer_session/connection.h>


class Log
{
	private:

		Genode::Attached_rom_dataspace _rom_ds;
		Genode::Log_connection         _log;

		char           _buffer [Genode::Log_session::MAX_STRING_LEN] { };
		unsigned short _buf_pos { 0 };
		unsigned       _rom_pos { 0 };

		bool const     _novae   { false };

		unsigned header_size() const
		{
			return (_novae ? 2 : 1) * 4;
		}

		unsigned log_size() const
		{
			return (unsigned)(_rom_ds.size() - header_size());
		}

		char const * char_from_rom(unsigned offset = 0) const
		{
			return _rom_ds.local_addr<char const>() + header_size() +
			       (_rom_pos + offset) % log_size();
		}

		unsigned next_pos(unsigned pos) const {
			return (pos + 1) % log_size(); }

		unsigned end_pos() const
		{
			if (!_novae)
				return *_rom_ds.local_addr<unsigned volatile>() % log_size();

			auto pos = novae_write_next_pos();
			if (pos == 0) pos = log_size(); else pos --;

			return pos;
		}

		unsigned novae_write_next_pos() const {
			return *(_rom_ds.local_addr<unsigned volatile>() + 1) % log_size(); }

		void _rom_to_log(unsigned const last_pos)
		{
			unsigned up_to_pos = last_pos;

			for (; _rom_pos != next_pos(up_to_pos)
				 ; _rom_pos = next_pos(_rom_pos), up_to_pos = end_pos()) {

				char const c = *char_from_rom();

				_buffer[_buf_pos++] = c;

				if (_buf_pos + 1U < sizeof(_buffer) && c != '\n')
					continue;

				_buffer[_buf_pos] = 0;
				_log.write(Genode::Log_session::String(_buffer));
				_buf_pos = 0;
			}
		}

	public:

		Log (Genode::Env &env, char const * const rom_name,
		     char const * const log_name, bool novae)
		: _rom_ds(env, rom_name), _log(env, log_name), _novae(novae)
		{
			unsigned const pos = end_pos();

			/* initial check whether already log wrapped at least one time */
			enum { COUNT_TO_CHECK_FOR_WRAP = 8 };
			for (unsigned i = 1; i <= COUNT_TO_CHECK_FOR_WRAP; i++) {
				if (*char_from_rom(pos + i) == 0)
					continue;

				/* wrap detected, set pos behind last known pos */
				_rom_pos = next_pos(pos + 1) % log_size();
				break;
			}

			_rom_to_log(pos);
		}

		void log() { _rom_to_log(end_pos()); }
};

struct Monitor
{
	Genode::Env &env;

	Genode::Attached_rom_dataspace config { env, "config" };

	Log output { env, "log", "log",
	             config.xml().attribute_value("type", Genode::String<9>("default")) == "novae" };

	Timer::Connection timer { env };

	Genode::Signal_handler<Monitor> interval { env.ep(), *this, &Monitor::check };

	Monitor(Genode::Env &env) : env(env)
	{
		timer.sigh(interval);

		auto period_ms = config.xml().attribute_value("period_ms", 1000UL);

		timer.trigger_periodic((Genode::uint64_t)1000 * period_ms);
	}

	void check()
	{
		output.log();
	}
};


void Component::construct(Genode::Env &env) { static Monitor output(env); }
