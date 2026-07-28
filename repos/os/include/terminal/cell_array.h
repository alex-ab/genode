/*
 * \brief  Array of character cells
 * \author Norman Feske
 * \date   2011-06-06
 */

/*
 * Copyright (C) 2011-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _TERMINAL__CELL_ARRAY_H_
#define _TERMINAL__CELL_ARRAY_H_

/* Genode includes */
#include <base/allocator.h>
#include <terminal/types.h>

namespace Terminal { template <typename> class Cell_array; }


/**
 * \param CELL  type of a single character cell that contains the information
 *              about the glyph and its attributes
 *
 * The 'CELL' type must have a default constructor and has to provide the
 * methods 'set_cursor()' and 'clear_cursor'.
 */
template <typename CELL>
class Terminal::Cell_array
{
	private:

		/*
		 * Noncopyable
		 */
		Cell_array(Cell_array const &);
		Cell_array &operator = (Cell_array const &);

		unsigned   _num_cols;
		unsigned   _num_lines;
		Allocator &_alloc;
		CELL     **_array      = nullptr;
		bool      *_line_dirty = nullptr;

		using Char_cell_line = CELL *;

		bool _valid(Region const region) const
		{
			return region.start <= region.end && region.end < _num_lines;
		}

		bool _valid(Position const pos) const
		{
			return pos.x < _num_cols && pos.y < _num_lines;
		}

		void _clear_line(Char_cell_line line)
		{
			for (unsigned col = 0; col < _num_cols; col++)
				*line++ = CELL();
		}

		void _mark_lines_as_dirty(Region const region)
		{
			if (_valid(region))
				for (unsigned line = region.start; line <= region.end; line++)
					_line_dirty[line] = true;
		}

		void _scroll_vertically(Region const region, bool up)
		{
			if (!_valid(region))
				return;

			/* rotate lines of the scroll region */
			Char_cell_line yanked_line = _array[up ? region.start : region.end];

			if (up) {
				for (unsigned line = region.start; line <= region.end - 1; line++)
					_array[line] = _array[line + 1];
			} else {
				for (unsigned line = region.end; line >= region.start + 1; line--)
					_array[line] = _array[line - 1];
			}

			_clear_line(yanked_line);

			_array[up ? region.end : region.start] = yanked_line;

			_mark_lines_as_dirty(region);
		}

	public:

		Cell_array(unsigned num_cols, unsigned num_lines, Allocator &alloc)
		:
			_num_cols(num_cols),
			_num_lines(num_lines),
			_alloc(alloc)
		{
			_array = new (alloc) Char_cell_line[num_lines];

			_line_dirty = new (alloc) bool[num_lines];
			mark_all_lines_as_dirty();

			for (unsigned i = 0; i < num_lines; i++)
				_array[i] = new (alloc) CELL[num_cols];
		}

		static size_t bytes_needed(unsigned num_cols, unsigned num_lines)
		{
			return sizeof(Char_cell_line[num_lines])
			     + sizeof(bool[num_lines])
			     + sizeof(CELL[num_cols])*num_lines;
		}

		~Cell_array()
		{
			for (unsigned i = 0; i < _num_lines; i++)
				destroy(_alloc, _array[i]);

			destroy(_alloc, _line_dirty);
			destroy(_alloc, _array);
		}

		void mark_all_lines_as_dirty()
		{
			for (unsigned i = 0; i < _num_lines; i++)
				_line_dirty[i] = true;
		}

		void set_cell(Position const pos, CELL cell)
		{
			if (_valid(pos)) {
				_array[pos.y][pos.x] = cell;
				_line_dirty[pos.y] = true;
			}
		}

		CELL get_cell(Position const pos) const
		{
			return _valid(pos) ? _array[pos.y][pos.x] : CELL { };
		}

		void import_from(Cell_array const &other)
		{
			unsigned const num_cols  = min(_num_cols,  other._num_cols),
			               num_lines = min(_num_lines, other._num_lines);

			for (unsigned line = 0; line < num_lines; line++)
				for (unsigned column = 0; column < num_cols; column++)
					_array[line][column] = other.get_cell({ column, line });

			mark_all_lines_as_dirty();
		}

		bool line_dirty(unsigned const line)
		{
			return line < _num_lines ? _line_dirty[line] : false;
		}

		void mark_line_as_clean(unsigned line)
		{
			if (line < _num_lines) _line_dirty[line] = false;
		}

		void mark_line_as_dirty(unsigned line)
		{
			if (line < _num_lines) _line_dirty[line] = true;
		}

		void scroll_up  (Region const r) { _scroll_vertically(r, true); }
		void scroll_down(Region const r) { _scroll_vertically(r, false); }

		void clear(Region region)
		{
			if (_valid(region))
				for (unsigned line = region.start; line <= region.end; line++)
					_clear_line(_array[line]);

			_mark_lines_as_dirty(region);
		}

		void cursor(Terminal::Position pos, bool enable, bool mark_dirty = false)
		{
			if (((unsigned)pos.x >= _num_cols) ||
			    ((unsigned)pos.y >= _num_lines))
				return;

			CELL &cell = _array[pos.y][pos.x];

			if (enable)
				cell.set_cursor();
			else
				cell.clear_cursor();

			if (mark_dirty)
				_line_dirty[pos.y] = true;
		}

		unsigned num_cols()  const { return _num_cols; }
		unsigned num_lines() const { return _num_lines; }
};

#endif /* _TERMINAL__CELL_ARRAY_H_ */
