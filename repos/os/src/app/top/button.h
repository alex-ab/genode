/*
 * \brief  Button state helper
 * \author Alexander Boettcher
 * \date   2019-03-18
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

struct Button_state
{
	unsigned  const first;
	unsigned  last;
	unsigned  current;
	unsigned  max     { 4 };
	bool      hovered { false };
	bool      prev    { false };
	bool      next    { false };

	Button_state(unsigned first, unsigned last)
	: first(first), last(last), current(first)
	{ }

	bool active() { return hovered || prev || next; }
	void reset() { hovered = prev = next = false; }

	bool advance()
	{
		bool update = false;

		if (prev && current > first) {
			current -= 1;
			update = true;
		}
		if (next && current < last) {
			current += 1;
			update = true;
		}

		return update;
	}

	void inc() { current = (current >= last) ? 0 : (current + 1); }
	void dec() { current = !current ? last : (current - 1); }
};

struct Button_hub
{
	enum { DIGITS = 5 };

	Button_state _button[DIGITS] { {0, 9}, {0, 9}, {0, 9}, {0, 9}, {0, 9} };

	bool update_inc()
	{
		bool update = false;
		for (unsigned i = 0; i < DIGITS; i++) {
			if (_button[i].hovered) {
				update = true;
				_button[i].inc();
			}
		}
		return update;
	}

	bool update_dec()
	{
		bool update = false;
		for (unsigned i = 0; i < DIGITS; i++) {
			if (_button[i].hovered) {
				update = true;
				_button[i].dec();
			}
		}
		return update;
	}

	void reset()
	{
		for (unsigned i = 0; i < DIGITS; i++) {
			_button[i].reset();
		}
	}

	void set(unsigned value)
	{
		for (unsigned i = 0; i < DIGITS; i++) {
			_button[i].current = value % 10;
			value /= 10;
		}
	}

	unsigned value() const
	{
		unsigned value = 0;
		for (unsigned i = DIGITS; i > 0; i--) {
			value *= 10;
			value += _button[i-1].current;
		}
		return value;
	}

	Button_state & button(unsigned i) { return _button[i]; }

	template <typename FUNC>
	void for_each(FUNC const &fn) {
		for (unsigned i = DIGITS; i > 0; i--) { fn(_button[i - 1], i - 1); } }
};
