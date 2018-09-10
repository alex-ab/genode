/*
 * \brief  Fullscreen nitpicker client exiting on right password
 * \author Alexander Boettcher
 * \date   2018-08-07
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <nitpicker_session/connection.h>

struct Main
{
	typedef Nitpicker::Session::View_handle View_handle;
	typedef Nitpicker::Session::Command     Command;

	typedef Genode::Signal_handler<Main>    Signal_handler;
	typedef Genode::Constructible<Nitpicker::Connection> Nitpicker_connection;
	typedef Genode::Attached_rom_dataspace  Attached_rom_dataspace;

	Genode::Env           &_env;
	View_handle            _handle           { };
	Nitpicker_connection   _nitpicker        { };
	Signal_handler         _input_handler  = { _env.ep(), *this,
	                                          &Main::_handle_input };
	Signal_handler         _mode_handler   = { _env.ep(), *this,
	                                           &Main::_handle_mode };
	Attached_rom_dataspace _config_rom     = { _env, "config" };
	Signal_handler         _config_handler = { _env.ep(), *this,
	                                           &Main::_update_config };

	bool                   _transparent    = { false };

	/* pwd state */
	Genode::uint32_t pwd[128];
	Genode::uint32_t pwd_i    { 0 };
	Genode::uint32_t pwd_max  { 0 };
	enum { WAIT_CLICK, RECORD_PWD, COMPARE_PWD } state { WAIT_CLICK };

	void _update_view(Genode::uint8_t const color)
	{
		Genode::Attached_dataspace fb_ds(_env.rm(),
		                                 _nitpicker->framebuffer()->dataspace());

		short *pixels = fb_ds.local_addr<short>();
		Genode::memset(pixels, color, fb_ds.size());

		using namespace Nitpicker;

		Framebuffer::Mode mode = _nitpicker->mode();
		Rect rect(Point(0, 0), Area(mode.width(), mode.height()));
		_nitpicker->enqueue<Command::Geometry>(_handle, rect);
		_nitpicker->enqueue<Command::To_front>(_handle, View_handle());
		_nitpicker->execute();
	}

	void _switch_view_record_pwd()
	{
		_update_view(~0);
		state = RECORD_PWD;
	}

	void _switch_view_compare_pwd()
	{
		_update_view(_transparent ? 0x10 : 0);
		state = COMPARE_PWD;
	}

	void _switch_view_initial()
	{
		_update_view(0x80);
		state = WAIT_CLICK;
	}

	void _inc_pwd_i()
	{
		pwd_i ++;
		if (pwd_i >= sizeof(pwd) / sizeof(pwd[0]))
			pwd_i = 0;
	}

	void _handle_input()
	{
		if (!_nitpicker.constructed())
			return;

		bool unlock = false;

		_nitpicker->input()->for_each_event([&] (Input::Event const &ev) {
			ev.handle_press([&] (Input::Keycode key, Input::Codepoint cp) {
				if (!ev.key_press(key) || !cp.valid())
					return;

				if (state == WAIT_CLICK)
					_switch_view_record_pwd();

				if (key == Input::Keycode::BTN_LEFT ||
				    key == Input::Keycode::BTN_RIGHT ||
				    key == Input::Keycode::BTN_MIDDLE)
					return;

				if (state == WAIT_CLICK)
					return;

				if (key == Input::Keycode::KEY_ESC) {
					pwd_i = 0;
				} else
				if (key == Input::Keycode::KEY_ENTER) {
					if (pwd_i > 0 && state == RECORD_PWD) {
						pwd_max  = pwd_i;
						_switch_view_compare_pwd();
					}
					pwd_i = 0;
				} else {
					if (state == RECORD_PWD) {
						pwd[pwd_i] = cp.value;
						_inc_pwd_i();
					}
					if (state == COMPARE_PWD) {
						if (pwd[pwd_i] != cp.value) {
							pwd_i = 0;
						} else {
							_inc_pwd_i();

							if (pwd_i == pwd_max)
								unlock = true;
						}
					}
				}
			});
		});

		if (!unlock || !_handle.valid())
			return;

		_nitpicker->destroy_view(_handle);
		_handle = View_handle();

		_nitpicker->mode_sigh(Genode::Signal_context_capability());
		_nitpicker->input()->sigh(Genode::Signal_context_capability());

		_nitpicker.destruct();

		_env.parent().exit(0);
	}

	void _handle_mode()
	{
		Framebuffer::Mode mode = _nitpicker->mode();
		_nitpicker->buffer(mode, _transparent);

		switch (state) {
		case COMPARE_PWD :
			_switch_view_compare_pwd();
			break;
		case RECORD_PWD :
			_switch_view_record_pwd();
			break;
		case WAIT_CLICK :
			_switch_view_initial();
			break;
		}
	}

	void _update_config()
	{
		_config_rom.update();

		if (!_config_rom.valid())
			return;

		Genode::String<sizeof(pwd)/sizeof(pwd[0])> passwd;

		Genode::Xml_node node = _config_rom.xml();
		passwd = node.attribute_value("password", passwd);

		bool transparent = _transparent;
		_transparent = node.attribute_value("transparent", _transparent);

		bool switch_view = transparent != _transparent;

		if (passwd.length() > 1) {
			for (unsigned i = 0; i < passwd.length(); i++)
				pwd[i] = passwd.string()[i];

			pwd_max = passwd.length() - 1;
			pwd_i   = 0;

			if (!switch_view)
				switch_view = state != COMPARE_PWD;

			state = COMPARE_PWD;
		}

		if (_handle.valid() && switch_view)
			_handle_mode();
	}

	Main(Genode::Env &env) : _env(env)
	{
		_nitpicker.construct(_env, "screen");

		View_handle parent_handle;
		_handle = _nitpicker->create_view(parent_handle);

		_config_rom.sigh(_config_handler);
		_nitpicker->mode_sigh(_mode_handler);
		_nitpicker->input()->sigh(_input_handler);

		_update_config();

		_handle_mode();
	}
};

void Component::construct(Genode::Env &env)
{
	static Main main(env);
}
