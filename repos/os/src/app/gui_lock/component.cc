/*
 * \brief  Fullscreen GUI client exiting on right passphrase
 * \author Alexander Boettcher
 * \date   2018-08-07
 */

/*
 * Copyright (C) 2018-2020 Genode Labs GmbH
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
	typedef Genode::Constructible<Nitpicker::Connection> Gui;
	typedef Genode::Constructible<Genode::Attached_dataspace> Fb;
	typedef Genode::Attached_rom_dataspace  Rom;

	Genode::Env    &_env;
	View_handle     _handle         { };
	Gui             _nitpicker      { };
	Fb              _framebuffer    { };
	Signal_handler  _input_handler  { _env.ep(), *this, &Main::_handle_input };
	Signal_handler  _mode_handler   { _env.ep(), *this, &Main::_handle_mode };
	Rom             _config_rom     { _env, "config" };
	Signal_handler  _config_handler { _env.ep(), *this, &Main::_update_config };

	bool            _transparent    { false };

	/* pwd state */
	enum { WAIT_CLICK, RECORD_PWD, COMPARE_PWD } state { WAIT_CLICK };
	struct {
		Genode::uint32_t chars [128];
		Genode::uint32_t i;
		Genode::uint32_t max;
	}    _pwd { };
	bool _cmp_valid { false };

	void _update_view(Genode::uint8_t const color)
	{
		short *pixels = _framebuffer->local_addr<short>();
		Genode::memset(pixels, color, _framebuffer->size());

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
		_cmp_valid = false;
	}

	void _show_box(unsigned const chars, short const cc, short const sc)
	{
		if (!chars) return;

		short *pixels = _framebuffer->local_addr<short>();

		Framebuffer::Mode mode = _nitpicker->mode();

		int const hs = 10;
		int const hsa = hs + 2;

		int const offset = (chars - 1) * hsa;
		if (offset + hsa*2 >= mode.width() / 2)
			return;

		unsigned const xpos = mode.width() / 2 - offset;
		unsigned const ypos = mode.height() / 2;

		for (int y=-hs; y < hs; y++)
			Genode::memset(pixels + mode.width() * (ypos + y) + xpos - hs,
			               cc, (chars * hsa * 2 + hs) * 2);

		for (int y=-hs; y < hs; y++) {
			for (unsigned c = 0; c < chars; c++) {
				unsigned x = xpos + c * hsa * 2;
				for (int i=-hs; i < hs; i++) {
					pixels[mode.width() * (ypos + y) + x + i ] = sc;
				}
			}
		}

		_nitpicker->framebuffer()->refresh(xpos - hs, ypos - hs,
		                                   chars * hsa * 2 + hs * 2, hs * 2);
	}

	void _switch_view_compare_pwd()
	{
		_update_view(_transparent ? 0x10 : 0);
		state = COMPARE_PWD;
		_cmp_valid = true;
		_pwd.i = 0;
	}

	void _switch_view_initial()
	{
		_update_view(0x80);
		state = WAIT_CLICK;
		_cmp_valid = false;
	}

	void _inc_pwd_i()
	{
		_pwd.i ++;
		if (_pwd.i >= sizeof(_pwd.chars) / sizeof(_pwd.chars[0]))
			_pwd.i = 0;
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

				bool reset = false;

				if (key == Input::Keycode::KEY_ESC) {
					reset = _pwd.i > 0;
					_pwd.i = 0;
				} else
				if (key == Input::Keycode::KEY_ENTER) {
					if (state == COMPARE_PWD) {
						unlock = _cmp_valid && (_pwd.i == _pwd.max);
						reset = true;
					}
					if (_pwd.i > 0 && state == RECORD_PWD) {
						_pwd.max = _pwd.i;
						_switch_view_compare_pwd();
					}

					_pwd.i = 0;
				} else {
					if (state == RECORD_PWD) {
						_pwd.chars[_pwd.i] = cp.value;
						_inc_pwd_i();
						_show_box(_pwd.i, ~0, 0);
					}
					if (state == COMPARE_PWD) {
						if (_cmp_valid)
							_cmp_valid = (_pwd.chars[_pwd.i] == cp.value);

						_inc_pwd_i();
						_show_box(_pwd.i, 0, ~0);
					}
				}
				if (reset) {
					if (state == RECORD_PWD)
						_switch_view_record_pwd();
					if (state == COMPARE_PWD)
						_switch_view_compare_pwd();
				}
			});
		});

		if (!unlock || !_handle.valid())
			return;

		_nitpicker->destroy_view(_handle);
		_handle = View_handle();

		_nitpicker->mode_sigh(Genode::Signal_context_capability());
		_nitpicker->input()->sigh(Genode::Signal_context_capability());

		_framebuffer.destruct();
		_nitpicker.destruct();

		Genode::memset(&_pwd, 0, sizeof(_pwd));

		_env.parent().exit(0);
	}

	void _handle_mode()
	{
		if (!_nitpicker.constructed())
			return;

		Framebuffer::Mode mode = _nitpicker->mode();
		_nitpicker->buffer(mode, _transparent);

		if (!_framebuffer.constructed())
			_framebuffer.construct(_env.rm(),
			                       _nitpicker->framebuffer()->dataspace());

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
		if (!_nitpicker.constructed())
			return;

		_config_rom.update();

		if (!_config_rom.valid())
			return;

		Genode::String<sizeof(_pwd.chars)/sizeof(_pwd.chars[0])> passwd;

		Genode::Xml_node node = _config_rom.xml();
		passwd = node.attribute_value("password", passwd);

		bool transparent = _transparent;
		_transparent = node.attribute_value("transparent", _transparent);

		bool switch_view = transparent != _transparent;

		if (passwd.length() > 1) {
			for (unsigned i = 0; i < passwd.length(); i++)
				_pwd.chars[i] = passwd.string()[i];

			_pwd.max = passwd.length() - 1;
			_pwd.i   = 0;

			if (!switch_view)
				switch_view = state != COMPARE_PWD;

			state = COMPARE_PWD;
		}

		if (_handle.valid() && switch_view)
			_handle_mode();
	}

	Main(Genode::Env &env) : _env(env)
	{
		Genode::memset(&_pwd, 0, sizeof(_pwd));

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
