/*
 * \brief  Utility to draw graphs in a coordinate system
 * \author Alexander Boettcher
 * \date   2019-03-12
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>

#include <base/trace/types.h>

#include <os/pixel_rgb565.h>
#include <nitpicker_session/connection.h>
#include <util/color.h>

#include <gems/vfs_font.h>

#include "trace.h"
#include "storage.h"

using Genode::Pixel_rgb565;
using Genode::Trace::Subject_id;

class Graph;

enum { MAX_GRAPHS = 8 };

struct Checkpoint
{
	Nitpicker::Point _points[MAX_GRAPHS];
	unsigned         _values[MAX_GRAPHS];
	Subject_id       _id[MAX_GRAPHS];
	Genode::uint64_t _time { 0 };
	Genode::uint8_t  _used { 0 };
	bool             _done { false };

	bool unused(unsigned const i) const { return i < MAX_GRAPHS && !_points[i].x() && !_points[i].y(); }
};

typedef Genode::Constructible<Genode::Attached_dataspace> Reconstruct_ds;
typedef Genode::Signal_handler<Graph> Signal_handler;

class Graph
{
	private:

		Genode::Env                     &_env;
		Genode::Heap                     _heap      { _env.ram(), _env.rm() };
		Genode::Attached_rom_dataspace   _config    { _env, "config" };
		Nitpicker::Connection            _gui       { _env };
		Input::Session_client           &_input     { *_gui.input() };
		Nitpicker::Session::View_handle  _view      { _gui.create_view() };
		Nitpicker::Session::View_handle  _view_text { _gui.create_view(_view) };

		Genode::Avl_tree<Entry>    _entries   { };
		Entry const                _entry_unknown { 0, "unknown", "", "" };

		int            _width        { 1000 };
		int            _height       { 425 };
		unsigned const _x_root       { 50 };
		unsigned const _y_detract    { 25 }; 
		unsigned const _scale_10_len { 10 };
		unsigned const _scale_5_len  { 5 };
		unsigned const _marker_half  { 4 };
		int      const _line_half    { 5 };
		int      const _invisible    { 200 };
		unsigned       _scale_e      { 2 };

		int           _max_width     { _width };
		int           _max_height    { _height };

		int height_mode()   const { return _height + _invisible; }
		unsigned y_root()   const { return _height - _y_detract; }
		unsigned scale_e()  const { return _scale_e; }
		unsigned scale_10() const { return (y_root() - _y_detract/2) / scale_e(); }
		unsigned scale_5()  const { return scale_10() / 2; }

		Signal_handler _signal_input { _env.ep(), *this, &Graph::_handle_input };
		Reconstruct_ds _ds           { };

		Signal_handler _signal_mode { _env.ep(), *this, &Graph::_handle_mode};

		Signal_handler _config_handler = { _env.ep(), *this, &Graph::_handle_config};

		Genode::Color const           _white     { 255, 255, 255 };
		Genode::Color const           _red       { 255,   0,   0 };
		Genode::Color const           _green     {   0, 255,   0 };
		Genode::Color const           _blue      {   0,   0, 255 };
		Genode::Color const           _black     {   0,   0,   0 };

		Checkpoint                    _column[256];
		Genode::uint16_t              _column_cur { 0 }; /* number of columns */
		Genode::uint16_t              _column_last { 0 };
		unsigned                      _hovered_vline { ~0U };
		Genode::uint64_t              _time_storage_wait_for { 0 };

		Genode::Root_directory _root { _env, _heap, _config.xml().sub_node("vfs") };
		Genode::Vfs_font       _font { _heap, _root, "fonts/monospace/regular" };

		Genode::Constructible<Top::Storage<Graph>> _storage { };

		/**
		 * nitpicker interface bring up
		 */
		Genode::Dataspace_capability _setup(int width, int height)
		{
			unsigned const xpos   = 0; //10;
			unsigned const ypos   = 0; // 325;

			using namespace Nitpicker;

			Area   area { 0U + width, 0U + height };
			Framebuffer::Mode mode { width, height_mode(),
			                         Framebuffer::Mode::RGB565 };

			_gui.buffer(mode, false /* no alpha */);

			Rect geometry(Point(xpos, ypos), area);
			_gui.enqueue<Session::Command::Geometry>(_view, geometry);
			_gui.execute();

			typedef Session::View_handle View_handle;
			_gui.enqueue<Session::Command::To_front>(_view, View_handle());
			_gui.execute();

			return _gui.framebuffer()->dataspace();
		}

		Signal_handler _graph_handler { _env.ep(), *this, &Graph::_handle_graph};
		Genode::Attached_rom_dataspace _graph { _env, "graph" };

		void _handle_input();
		void _handle_graph();
		void _handle_mode();
		void _handle_data();
		void _handle_config();

		Genode::Color _color(unsigned i)
		{
			i %= MAX_GRAPHS;
			switch (i) {
			case 0: return _red;
			case 1: return _green;
			case 2: return _blue;
			case 3: return Genode::Color {   0, 255, 255 };
			case 4: return Genode::Color { 255,   0, 255 };
			case 5: return Genode::Color { 255,   0, 128 };
			case 6: return Genode::Color { 255, 128,   0 };
			default:
				return Genode::Color { 255, 255,   0 };
			}
		}

		void _init_screen(bool reset_points = true)
		{
			/* vertical line and scale */
			vline(_x_root , _white);
			for (unsigned i = y_root() - scale_10() * scale_e(); i < y_root(); i += scale_10()) {

				Nitpicker::Point point(_x_root, i);
				hline(point, _scale_10_len, _white);
				hline_dotted(point, _width - _x_root, _white, 8);

				Genode::String<4> text(((y_root() - i) / scale_10()) * 10);

				unsigned const text_size = _font.bounding_box().w() *
				                           (text.length() - 1) + _scale_10_len;
				int xpos = 0;
				int ypos = 0;
				if (_x_root > text_size) xpos = _x_root - text_size;
				if (i > _font.height() / 2) ypos = i - _font.height() / 2;

				_text(text.string(), Text_painter::Position(xpos, ypos), _white);

				Nitpicker::Point point_5(_x_root, i + scale_10() - scale_5());
				hline(point_5, _scale_5_len, _white);
				hline_dotted(point_5, _width - _x_root, _white, 8);
			}

			/* horizontal line */
			hline(y_root() , _white);

			if (reset_points)
				Genode::memset(_column, 0, sizeof(_column));
		}

		Genode::String<8> _percent(unsigned percent, unsigned rest) {
			return Genode::String<8> (percent < 10 ? "  " : (percent < 100 ? " " : ""),
			                          percent, ".", rest < 10 ? "0" : "", rest, "%");
		}

		Entry * find_by_id(Subject_id const id) {
			Entry * entry = _entries.first();
			if (entry)
				entry = entry->find_by_id(id);
			return entry;
		}

	public:

		Graph(Genode::Env &env) : _env(env)
		{
			Genode::memset(_column, 0, sizeof(_column));

			_graph.sigh(_graph_handler);
			_gui.mode_sigh(_signal_mode);
			_input.sigh(_signal_input);
			_config.sigh(_config_handler);

			_handle_config();
		}

		void hline(unsigned const y, Genode::Color const &color)
		{
			Pixel_rgb565 * pixel = _ds->local_addr<Pixel_rgb565>() + y * _width;
			for (int i = 0; i < _width; i++)
				*(pixel + i) = Pixel_rgb565(color.r, color.g, color.b, color.a);
		}

		void hline(Nitpicker::Point const point, int len,
		           Genode::Color const &color)
		{
			Pixel_rgb565 * pixel = _ds->local_addr<Pixel_rgb565>()
			                       + point.y() * _width + point.x();

			for (int i = -len; i <= len; i++)
				*(pixel + i) = Pixel_rgb565(color.r, color.g, color.b, color.a);
		}

		void hline_dotted(Nitpicker::Point const point, unsigned len,
		                  Genode::Color const &color, unsigned step)
		{
			Pixel_rgb565 * pixel = _ds->local_addr<Pixel_rgb565>()
			                       + point.y() * _width + point.x();

			for (unsigned i = 0; i < len; i += step)
				*(pixel + i) = Pixel_rgb565(color.r, color.g, color.b, color.a);
		}

		void vline(unsigned const x, Genode::Color const &color)
		{
			Pixel_rgb565 * pixel = _ds->local_addr<Pixel_rgb565>() + x;
			for (int i = 0; i < _height; i ++)
				*(pixel + i * _width) = Pixel_rgb565(color.r, color.g,
				                                     color.b, color.a);
		}

		void _text(char const * const text, Text_painter::Position const pos,
		           Genode::Color const &color)
		{
			Genode::Surface_base::Area const size { 0U + _width, 0U + height_mode() };
			Genode::Surface<Pixel_rgb565> surface { _ds->local_addr<Pixel_rgb565>(), size };
			Text_painter::paint(surface, pos, _font, color, text);
		}

		void marker(Nitpicker::Point const point, int len,
		            Genode::Color const &color)
		{
			Pixel_rgb565 * pixel = _ds->local_addr<Pixel_rgb565>()
			                       + point.y() * _width + point.x();

			Pixel_rgb565 const dot(color.r, color.g, color.b, color.a);
/*
			for (int y = -len; y <= len; y++) {
				*(pixel + y + y * _width) = dot;
				*(pixel - y + y * _width) = dot;
			}
*/
			for (int y = -len; y <= len; y++) {
				*(pixel + y) = dot;
			}
		}

		void marker(Nitpicker::Point const fr, Nitpicker::Point const to,
		            Genode::Color const &color)
		{
			Pixel_rgb565 *p_f = _ds->local_addr<Pixel_rgb565>()
			                    + fr.y() * _width + fr.x();

			Pixel_rgb565 const dot(color.r, color.g, color.b, color.a);

			int const w = to.x() - fr.x() + 1;
			if (w <= 0) return;

			int const height = to.y() - fr.y();
			int const h = (height < 0) ? height - 1 : height + 1;
			int const start = (height < 0) ? h : 0;
			int const end = (height < 0) ? 0 : h;
			int const f = h / w;

			if (height == 0) {
				for (int x = 0; x < w; x++)
					*(p_f + x) = dot;
			} else if (f == 0) {
				int const b = w / h;
				int const r = w % h;
				for (int y = start; y < end; y++) {
					for (int x = 0; x < w; x++) {
						int const o = r * y / h;

						if (y == (x - o) / b)
							*(p_f + x + y * _width) = dot;
					}
				}
			} else {
				int const r = (h % w);
				for (int y = start; y < end; y++) {
					for (int x = 0; x < w; x++)
					{
						int const o = r * x / w;
						int s, e;

						if (height < 0) {
							s = o + f * (x + 1) + 1;
							e = o + f * x;
							if (x == w - 1) s = start + 1;
						} else {
							s = o + f * x;
							e = o + f * (x + 1) - 1;
							if (x == w - 1) e = end - 1;
						}

						if (s <= y && y <= e)
							*(p_f + x + y * _width) = dot;
					}
				}
			}
		}

		Nitpicker::Point _data(unsigned const time, unsigned const graph) const {
			return _column[time]._points[graph]; }

		unsigned _value(unsigned const time, unsigned const graph) const {
			return _column[time]._values[graph]; }

		Subject_id _id(unsigned const time, unsigned const graph) const {
			return _column[time]._id[graph]; }

		void _replay_data();
		bool _apply_data(Subject_id, unsigned);

		Nitpicker::Point _apply_data_point(unsigned const value, unsigned element);

private:

		Checkpoint &prev_entry()
		{
			unsigned const prev = _column_cur ? _column_cur - 1
			                                  : sizeof(_column) / sizeof(_column[0]) - 1;
			return _column[prev];
		}

		void _advance_element_column(Genode::uint64_t const time) {

			if (_column[_column_cur]._used) {
				_column[_column_cur]._done = true;

				if (_column_cur) {
					Checkpoint &data_prev = prev_entry();
					Checkpoint &data = _column[_column_cur];

					/* end marker for entries which are not continued */
					for (unsigned i = 0; i < MAX_GRAPHS; i++) {
						if (data_prev.unused(i)) continue;

						bool end_marker = true;
						for (unsigned j = 0; j < MAX_GRAPHS; j++) {
							if (data.unused(j)) continue;
							if (data_prev._id[i] == data._id[j]) {
								end_marker = false;
								break;
							}
						}
						if (end_marker) {
							Genode::Color const color = _color(i);
							marker(data_prev._points[i], _marker_half, color);
						}
					}
				}

				/* next column */
				_column_cur = (_column_cur + 1) % (sizeof(_column) / sizeof(_column[0]));

				/* reset screen check - XXX scrolling would be nice */
				Nitpicker::Point point = _apply_data_point(0, _column_cur);
				if (!point.x() && !point.y()) {
					/* wrap to next column */
					_column_cur = 0;
					_scale_e = 2;

					for (; Entry * entry = _entries.first(); ) {
					    _entries.remove(entry);
					    Genode::destroy(_heap, entry);
					}

					Genode::memset(_ds->local_addr<void>(), 0, _width * _height * sizeof(Pixel_rgb565));
					_init_screen();
					_gui.framebuffer()->refresh(0, 0, _width, _height);
				}
			}

			_column[_column_cur]._time = time;
			_column[_column_cur]._done = false;
		}

public:

		bool advance_column_by_storage(Genode::uint64_t const time);

		Genode::uint64_t time() const { return _column[_column_cur]._time; }

		Genode::uint64_t time(unsigned const pos) const {
			return _column[pos]._time; }

		/*
		 * Used also by storage layer
		 */

		bool new_data(unsigned const, unsigned const, unsigned long long);

		bool id_available(Subject_id const id)
		{
			Entry * entry = _entries.first();
			if (entry)
				entry = entry->find_by_id(id);
			return entry != nullptr;
		}

		void add_entry(Subject_id const id, Genode::Session_label const &label,
		               Genode::Trace::Thread_name const &thread,
		               Genode::String<12> const &cpu)
		{
			if (id_available(id)) return;

			_entries.insert(new (_heap) Entry(id, thread, label, cpu));
		}
};

void Graph::_handle_config()
{
	_config.update();

	if (!_config.valid()) return;

	bool const store = _config.xml().attribute_value("store", false);

	if (store && !_storage.constructed())
		_storage.construct(_env, *this);
	if (!store && _storage.constructed())
		_storage.destruct();
}

void Graph::_handle_mode()
{
	Framebuffer::Mode const mode = _gui.mode();

	if (mode.width() == _width && mode.height() == _height)
		return;

	if (mode.width() < 100 || mode.height() < 100)
		return;

	if (mode.width() * mode.height() > _max_width * _max_height) {
		unsigned diff = (mode.width() * mode.height() -
		                 _max_width * _max_height) * sizeof(Pixel_rgb565);
		if (diff > _env.pd().avail_ram().value + 0x2000) {
			Genode::warning("no memory left for mode change - ",
			                _width, "x", _height, " -> ",
			                mode.width(), "x", mode.height(), " - ",
			                _env.pd().avail_ram(), " (available) < ",
			                diff, " (required)");
			return;
		}
		_max_width  = _width;
		_max_height = _height;
	}

	_width = mode.width();
	_height = mode.height();

	if (!_ds.constructed())
		return;

	_ds.destruct();
	_ds.construct(_env.rm(), _setup(_width, _height));

	Genode::memset(_ds->local_addr<void>(), 0, _width * _height * sizeof(Pixel_rgb565));
	_init_screen(false);
	_replay_data();
	_gui.framebuffer()->refresh(0, 0, _width, _height);
}

Nitpicker::Point Graph::_apply_data_point(unsigned const value, unsigned element)
{
	unsigned const step = 20;

	/* use all possible pixels by applying adaptive factor */
	unsigned factor = 1;
	if ((scale_5() / 5) > 1)
		factor = scale_5() / 5;

	unsigned percent = value / 100 * factor;
	if (percent > (scale_e() * 10) * factor)
		percent = (scale_e() * 10) * factor + _y_detract / 3; /* show bit above upper line */

	unsigned const f10 = 10 * factor;
	unsigned const f5  =  5 * factor;

	/* 10 base */
	unsigned y = scale_10() * (percent / f10);
	/* 5 base */
	y += scale_5() * ((percent % f10) / f5);
	/* rest */
	y += (scale_5() / f5) * (percent % f5);

	unsigned x1 = _x_root + (((element + 1)*step) % (_width - _x_root));
	unsigned x2 = _x_root + (((element + 0)*step) % (_width - _x_root));
	if (x2 > x1)
		return Nitpicker::Point(0, 0);
	return Nitpicker::Point(x1, y_root() - y);
}

void Graph::_replay_data()
{
	for (unsigned i = 0; i < _column_cur; i++)
	{
		for (unsigned graph = 0; graph < MAX_GRAPHS; graph++)
		{
			if (_column[i].unused(graph)) continue;

			Genode::Color color = _color(graph);
			Nitpicker::Point point = _apply_data_point(_value(i, graph), i);

			marker(point, _marker_half, color);

			_column[i]._points[graph] = point;
		}
	}
}

bool Graph::_apply_data(Subject_id const id, unsigned const value)
{
	Nitpicker::Point const point = _apply_data_point(value, _column_cur);
	Checkpoint &data = _column[_column_cur];

	unsigned entry = MAX_GRAPHS;
	unsigned same = MAX_GRAPHS;
	Checkpoint &data_prev = prev_entry();

	{
		unsigned free = 0;
		bool fix = false;

		/* find free entry and lookup if used previously */
		for (unsigned i = 0; i < MAX_GRAPHS; i++) {
			if (data_prev._id[i] == id) same = i;

			if (data.unused(i)) {
				free ++;
				if (data_prev._id[i] == id) { entry = i; fix = true; }
				if (entry >= MAX_GRAPHS) { entry = i; }
				if (!fix && data_prev.unused(i)) { entry = i; fix = true; }
			} else {
				/* may happen due to reading from rom and from storage */
				if (data._id[i] == id) { entry = i; fix = true; }
			}
		}

		if ((entry < MAX_GRAPHS) && (same < MAX_GRAPHS) && (entry != same)) {
			/* try to get same position, if enough free entries */
			if (free > 1 && !fix) {
				Genode::warning("check check move ", free, " same=", same, " entry=", entry);
				for (unsigned i = 0; i < MAX_GRAPHS; i++) {
					if (!data.unused(i)) continue;
					if (i == entry) continue;

					data._points[i] = data._points[same];
					data._values[i] = data._values[same];
					data._id[i] = data._id[same];

					entry = same;
					break;
				}
			}
		}
	}

	if (entry >= MAX_GRAPHS) return false;

	data._points[entry] = point;
	data._values[entry] = value;
	data._id[entry] = id;
	data._used ++;

	{
		Genode::Color const color = _color(entry);

		if (same < MAX_GRAPHS)
			marker(data_prev._points[same], point, color);
		else
			marker(point, _marker_half, color);
	}

	return true;
}

bool Graph::advance_column_by_storage(Genode::uint64_t const time)
{
	if (time > this->time()) {
		_advance_element_column(time);
	}

	if (time == _time_storage_wait_for) {
		/* read enough from storage file -> trigger graphical update */
		_handle_data();
		return false;
	}

	return true;
}

bool Graph::new_data(unsigned const value, unsigned const id, Genode::uint64_t const tsc)
{
	if (tsc != time())
		_advance_element_column(tsc);

	if (id == Top::Storage<Graph>::INVALID_ID) return false;
	if (_column[_column_cur]._done) return false;

	Subject_id const subject {id};
	return _apply_data(subject, value);
}

void Graph::_handle_graph()
{
//	Genode::warning("_handle_graph ", _column_cur);

	_graph.update();
	if (!_graph.valid())
		return;

	/* no values - no graph view */
	if (!_graph.xml().num_sub_nodes()) {
		/* destruct current ds until new data arrives */
		if (_ds.constructed()) {
			_ds.destruct();
			_setup(0, 0);
		}
		return;
	}

	/* new data means new graph if not setup currently */
	if (!_ds.constructed()) {
		_ds.construct(_env.rm(), _setup(_width, _height));
		_init_screen();
		_gui.framebuffer()->refresh(0, 0, _width, _height);
	}

	if (_storage.constructed()) {
		Genode::Xml_node node = _graph.xml().sub_node("entry");
		unsigned long long const tsc = node.attribute_value("tsc", 0ULL);

		if (time() < tsc) {
			_time_storage_wait_for = tsc;

//			Genode::error(Genode::Hex(time()), " < ", Genode::Hex(tsc));
			_storage->ping();
			return;
		}
	}

	_handle_data();
}

void Graph::_handle_data()
{
//	Genode::warning("_handle_data ", _column_last, "->", _column_cur);

	bool scale_update = false;

	unsigned refresh = 0;
	unsigned too_many = 0;

	do {
		if (scale_update) {
			Genode::memset(_ds->local_addr<void>(), 0, _width * _height * sizeof(Pixel_rgb565));
			_init_screen(_column_cur == 0);
			_replay_data();
			_gui.framebuffer()->refresh(0, 0, _width, _height);
		}

		scale_update = false;
		refresh = 0;
		too_many = 0;
		unsigned scale_above = 0;

		_graph.xml().for_each_sub_node("entry", [&](Genode::Xml_node &node){
			too_many ++;
			if (refresh >= MAX_GRAPHS)
				return;

			unsigned value = 0;
			unsigned id = 0;
			Genode::uint64_t tsc = 0;

			node.attribute("value").value(value);
			node.attribute("id").value(id);
			node.attribute("tsc").value(tsc);

			{
				Entry * entry = find_by_id(id);
				/* XXX - read out from storage if available */
				if (!entry) {
					Genode::String<12> cpu;
					Genode::String<64> session_label;
					Genode::Trace::Thread_name thread_name;

					node.attribute("cpu").value(cpu);
					node.attribute("label").value(session_label);
					node.attribute("thread").value(thread_name);

					Genode::Session_label const label(session_label);
					add_entry(id, label, thread_name, cpu);
				}
			}

			if (new_data(value, id, tsc)) {
				refresh ++;
				too_many --;
			}

			/* XXX heuristic when do re-create scale */
			if (value / 100 > _scale_e * 10) {
				if (value / 100 - _scale_e * 10 <= 15) {
					_scale_e = Genode::min(10U, value / 1000 + 1);
					scale_update = true;
				} else
					scale_above ++;

				if (scale_above > 1) {
					_scale_e = Genode::min(10U, value / 1000 + 1);
					scale_update = true;
				}
			}
		});
	} while (scale_update);

	/* better a function call XXX */
	_column[_column_cur]._done = true;

	if (!refresh)
		return;

	if (_column_last > _column_cur) {
		_gui.framebuffer()->refresh(0, 0, _width, _height);
		return;
	}

	unsigned xpos_s = _apply_data_point(10 /* does not matter value */, _column_last).x();
	unsigned xpos_e = _apply_data_point(10 /* does not matter value */, _column_cur).x();
	_gui.framebuffer()->refresh(xpos_s - _marker_half, 0,
	                            xpos_e - xpos_s + 2 * _marker_half + 1, _height);

	_column_last = _column_cur;
}

void Graph::_handle_input()
{
	bool absevent = false;
	bool hovered  = false;
	unsigned hovered_old = _hovered_vline;
	unsigned last_y      = 0;

	unsigned hovered_old_entry = ~0U;
	unsigned hovered_entry     = ~0U;

	_input.for_each_event([&] (Input::Event const &ev) {
		ev.handle_absolute_motion([&] (int x, int y) {

			/* consume events but drop it if we have no data to show */
			if (!_ds.constructed())
				return;

			absevent = true;

			for (unsigned i = 0; i <= _column_cur; i++) {
				for (unsigned j = 0; j < MAX_GRAPHS; j++) {
					if (_column[i].unused(j)) continue;

					Nitpicker::Point p = _data(i, j);

					if (i == hovered_old && hovered_old_entry >= MAX_GRAPHS)
						hovered_old_entry = j;

					if ((p.x() - _line_half <= x) && (p.x() + _line_half >= x))
					{
						hovered_entry  = j;
						hovered        = true;
						_hovered_vline = i;
						last_y         = y;
					}

					if (hovered && hovered_old_entry < MAX_GRAPHS) {
						i = _column_cur;
						break;
					}
				}
			}
		});
	});

	if (!absevent || (hovered && hovered_entry >= MAX_GRAPHS) ||
	    (hovered_old < _column_cur && hovered_old_entry >= MAX_GRAPHS))
		return;

	if (!hovered && (hovered_old <= _column_cur)) {
		unsigned const x = _data(hovered_old, hovered_old_entry).x();
		vline(x - _line_half, _black);
		vline(x + _line_half, _black);

		_hovered_vline = ~0U; /* invalid */

		_gui.framebuffer()->refresh(x - _line_half, 0,
		                                  _line_half * 2 + 1, _height);

		/* hiding a view would be nice - destroy and re-create */
		using namespace Nitpicker;
		typedef Session::View_handle View_handle;

		/* HACK - wm does not work properly for destroy_view ... nor to_back XXX */
		Point point(0, _height);
		Rect geometry_text(point, Area { 1, 1 });
		_gui.enqueue<Session::Command::Geometry>(_view_text, geometry_text);

		_gui.enqueue<Session::Command::To_front>(_view, View_handle());

		_gui.execute();
	}

	if (hovered && _hovered_vline != hovered_old) {
		if (hovered_old <= _column_cur) {
			unsigned const x = _data(hovered_old, hovered_old_entry).x();
			vline(x - _line_half, _black);
			vline(x + _line_half, _black);
			_gui.framebuffer()->refresh(x - _line_half, 0,
			                                  _line_half * 2 + 1, _height);
		}
		unsigned const x = _data(_hovered_vline, hovered_entry).x();
		vline(x - _line_half, _white);
		vline(x + _line_half, _white);

		_gui.framebuffer()->refresh(x - _line_half, 0,
		                                  _line_half * 2 + 1, _height);

		memset(_ds->local_addr<Pixel_rgb565>() + _height * _width, 0,
		       (height_mode() - _height) * _width * sizeof(Pixel_rgb565));

		unsigned text_count = 0;
		unsigned max_len    = 0;
		unsigned skipped    = 0;

		/* show info about timestamp */
		{
			unsigned long long const freq_khz = 2000000;
			Genode::String<32> string(time(_hovered_vline) / freq_khz, " ms",
			                          " (", Genode::Hex(time(_hovered_vline)), ")");
			max_len = Genode::max(string.length(), max_len);

			int const ypos = _height + 5;
			if (ypos + (_font.height() + 5) < 0U + height_mode())
				_text(string.string(), Text_painter::Position(0, ypos), _white);

			text_count ++;
		}

		/* show infos about threads */
		for (unsigned i = 0; i < MAX_GRAPHS; i++) {
			if (_column[_hovered_vline].unused(i)) {
				skipped ++;
				continue;
			}

			Entry const * entry = find_by_id(_id(_hovered_vline, i));
			if (!entry) {
				entry = &_entry_unknown;
				Genode::log("unknown id ", _id(_hovered_vline, i).id);
			}

			unsigned const cmp = entry->cpu().length() > 6 ? 8 : 5;
			Genode::String<128> string(_percent(_value(_hovered_vline, i) / 100,
			                                    _value(_hovered_vline, i) % 100), " ",
			                           entry->cpu().length() < cmp ? " " : "",
			                           entry->cpu(), " ",
			                           entry->thread_name(), ", ",
			                           entry->session_label());

			max_len = Genode::max(string.length(), max_len);

			int const ypos = _height + 5 + (i + 1 - skipped) * (_font.height() + 5);
			if (ypos + (_font.height() + 5) < 0U + height_mode())
				_text(string.string(), Text_painter::Position(0, ypos), _color(i));

			text_count ++;
		}

		unsigned const width = Genode::min(0U + _width, (_font.bounding_box().w() - 1) * max_len);
		unsigned const height = Genode::min(0U + height_mode() - _height, (_font.height() + 5) * text_count);

		using namespace Nitpicker;

		Area area_text { width, height };

		int xpos = _data(_hovered_vline, hovered_entry).x() + 20;
		if (xpos + (int)area_text.w() > _width)
			xpos = xpos - 20 - area_text.w();
		int ypos = last_y;
		if (last_y + area_text.h() > 0U + _height)
			ypos = _height - area_text.h();

		Point point(xpos, ypos);
		Rect geometry_text(point, area_text);

		_gui.enqueue<Session::Command::Offset>(_view_text, Point(0, -_height));
		_gui.enqueue<Session::Command::Geometry>(_view_text, geometry_text);
		_gui.enqueue<Session::Command::To_front>(_view_text, _view);
		_gui.execute();
	}
}

void Component::construct(Genode::Env &env) { static Graph component(env); }
