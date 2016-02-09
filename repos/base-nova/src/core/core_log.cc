/*
 * \brief  Kernel-specific core's 'log' backend
 * \author Stefan Kalkowski
 * \date   2016-10-10
 */

/*
 * Copyright (C) 2016 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* core includes */
#include <core_log.h>

/* Genode includes */
#include <base/printf.h>
#include <bios_data_area.h>
#include <drivers/uart_base.h>

extern "C" unsigned short * vga_base;

namespace Genode { class Vga; }

class Genode::Vga
{
	private:

		void _outb(uint16_t port, uint8_t val)
		{
			asm volatile ("outb %b0, %w1" : : "a" (val), "Nd" (port));
		}

		inline void _vga_configure(unsigned reg_offset, unsigned reg_index,
		                           unsigned value)
		{
			/* select the register by programming the index of the wanted register */
			_outb(0x3c0 + reg_offset, reg_index);
			/* write the value to the actual register */
			_outb(0x3c1 + reg_offset, value);
		}

		unsigned _vga_pos;
		unsigned _buf_pos;

		unsigned short _color;
		unsigned char _buf[10];

		struct {
			const char * color_string;
			unsigned short color;
		} _map [6];

	public:

		void put_char(char c)
		{
			if (!vga_base) return;

			/* remember last 4 chars */
			_buf[_buf_pos++] = c;

			unsigned i;
			for (i=0; i < sizeof(_map) / sizeof(_map[0]); i++) {
				/* look for color string */
				if (memcmp(_buf, _map[i].color_string, _buf_pos)) continue;
				break;
			}

			/* check for matching color string */
			if (i < sizeof(_map) / sizeof(_map[0])) {
				if ((_buf_pos == 4) && (i == 5)) {
					_color = _map[i].color;
					_buf_pos = 0;
				} else
				if (_buf_pos == 5) {
					_color = _map[i].color;
					_buf_pos = 0;
				}
				return;
			}

			/* if nothing was found we're done */
			if (_buf_pos > 1) {
				_buf_pos = 0;
				return;
			}
			/* reset buffer index */
			_buf_pos = 0;

			if (c == '\n')
				/* clear the line */
				while (_vga_pos % 80)
					vga_base[_vga_pos++] = _color | ' ';
			else
				vga_base[_vga_pos++] = _color | c;

			_vga_pos %= 24 * 80;

			/* set the cursor after last character */
			_vga_configure(0x14, 0xe, _vga_pos >> 8);
			_vga_configure(0x14, 0xf, _vga_pos);
		}

		Vga()
		:
			_vga_pos(0), _buf_pos(0), _color(0xf00)
		{
			_map [0].color_string = ESC_WRN;
			_map [0].color        = 0x100;
			_map [1].color_string = ESC_INF;
			_map [1].color        = 0x200;
			_map [2].color_string = ESC_LOG;
			_map [2].color        = 0xe00;
			_map [3].color_string = ESC_ERR;
			_map [3].color        = 0x400;
			_map [4].color_string = ESC_DBG;
			_map [4].color        = 0x100;
			_map [5].color_string = ESC_END;
			_map [5].color        = 0xf00;
		}
};

void Genode::Core_log::out(char const c)
{
	enum { CLOCK = 0, BAUDRATE = 115200 };

	static X86_uart_base uart(Bios_data_area::singleton()->serial_port(),
	                          CLOCK, BAUDRATE);
	static Vga vga;

	if (c == '\n') {
		uart.put_char('\r');
		vga.put_char('\r');
	}
	uart.put_char(c);
	vga.put_char(c);
}
