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

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>

#include <base/trace/types.h>

#include <os/pixel_rgb565.h>
#include <nitpicker_session/connection.h>
#include <util/color.h>

#include <gems/vfs_font.h>

using Genode::Pixel_rgb565;

class Graph;

enum { MAX_GRAPHS = 8 };

struct Checkpoint
{
	Nitpicker::Point _points[MAX_GRAPHS];
	unsigned         _values[MAX_GRAPHS];
};

typedef Genode::Constructible<Genode::Attached_dataspace> Reconstruct_ds;
typedef Genode::Signal_handler<Graph> Signal_handler;

class Graph
{
	private:

		Genode::Env                     &_env;
		Genode::Heap                     _heap      { _env.ram(), _env.rm() };
		Genode::Attached_rom_dataspace   _config    { _env, "config" };
		Nitpicker::Connection            _nitpicker { _env };
		Input::Session_client           &_input     { *_nitpicker.input() };
		Nitpicker::Session::View_handle  _view      { _nitpicker.create_view() };
		Nitpicker::Session::View_handle  _view_text { _nitpicker.create_view(_view) };

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

		Genode::Color const           _white     { 255, 255, 255 };
		Genode::Color const           _red       { 255,   0,   0 };
		Genode::Color const           _green     {   0, 255,   0 };
		Genode::Color const           _blue      {   0,   0, 255 };
		Genode::Color const           _black     {   0,   0,   0 };

		Genode::String<6>             _cpu[MAX_GRAPHS];
		Genode::Session_label         _session_labels[MAX_GRAPHS];
		Genode::Trace::Thread_name    _thread_names[MAX_GRAPHS];
		Genode::uint64_t              _graph_used[MAX_GRAPHS];

		Checkpoint                    _points[256];
		unsigned                      _elements { 0 }; /* number of points */
		unsigned                      _hovered_vline { ~0U };

		Genode::Root_directory _root { _env, _heap, _config.xml().sub_node("vfs") };
		Genode::Vfs_font       _font { _heap, _root, "fonts/monospace/regular" };

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

			_nitpicker.buffer(mode, false /* no alpha */);

			Rect geometry(Point(xpos, ypos), area);
			_nitpicker.enqueue<Session::Command::Geometry>(_view, geometry);
			_nitpicker.execute();

			typedef Session::View_handle View_handle;
			_nitpicker.enqueue<Session::Command::To_front>(_view, View_handle());
			_nitpicker.execute();

			return _nitpicker.framebuffer()->dataspace();
		}

		Signal_handler _graph_handler { _env.ep(), *this, &Graph::_handle_graph};
		Genode::Attached_rom_dataspace _graph { _env, "graph" };

		void _handle_input();
		void _handle_graph();
		void _handle_mode();

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
				Genode::memset(_points, 0, sizeof(_points));
		}

		Genode::String<8> _percent(unsigned percent, unsigned rest) {
			return Genode::String<8> (percent < 10 ? "  " : (percent < 100 ? " " : ""),
			                          percent, ".", rest < 10 ? "0" : "", rest, "%");
		}

	public:

		Graph(Genode::Env &env) : _env(env)
		{
			_graph.sigh(_graph_handler);
			_nitpicker.mode_sigh(_signal_mode);
			_input.sigh(_signal_input);

			Genode::memset(_graph_used, 0, sizeof(_graph_used));
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

		Nitpicker::Point _data(unsigned time, unsigned element) {
			return _points[time]._points[element]; }

		unsigned _value(unsigned time, unsigned element) {
			return _points[time]._values[element]; }

		void _replay_data();
		void _apply_data(int value, Genode::Color const &color, int entry);
		Nitpicker::Point _apply_data_point(int const value, unsigned element);

		void _apply_test_data(int *, Genode::Color const &,
		                      Genode::Trace::Thread_name &,
		                      Genode::Session_label &,
		                      Genode::String<6> &,
		                      unsigned);
		void apply_test_data();
};

void Graph::_handle_mode()
{
	Framebuffer::Mode const mode = _nitpicker.mode();

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
	_nitpicker.framebuffer()->refresh(0, 0, _width, _height);
}

void Graph::_apply_test_data(int * values, Genode::Color const &color,
                             Genode::Trace::Thread_name &thread_name,
                             Genode::Session_label &session_label,
                             Genode::String<6> &cpu,
                             unsigned entry)
{
	unsigned const step = (_width - _x_root) / (_elements + 1);

	/* use all possible pixels by applying adaptive factor */
	unsigned factor = 1;
	if ((scale_5() / 5) > 1)
		factor = scale_5() / 5;

	for (unsigned i = 0; i < _elements; i++) {
		if (0U + values[i] > scale_e() * 10)
			values[i] = scale_e() * 10;
		else
			values[i] *= factor;
	}
	unsigned const f10 = 10 * factor;
	unsigned const f5  =  5 * factor;

	/* apply the values */
	for (unsigned i = 0; i < _elements; i++) {
		/* 10 base */
		unsigned y = scale_10() * (values[i] / f10);
		/* 5 base */
		y += scale_5() * ((values[i] % f10) / f5);
		/* rest */
		y += (scale_5() / f5) * (values[i] % f5);

		Nitpicker::Point point(_x_root + (i + 1) * step, y_root() - y);
		marker(point, _marker_half, color);

		_points[i]._points[entry] = point;
	}

	_thread_names[entry]   = thread_name;
	_session_labels[entry] = session_label;
	_cpu[entry]            = cpu;

}

Nitpicker::Point Graph::_apply_data_point(int const value, unsigned element)
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
	for (unsigned i = 0; i < _elements; i++) {
		for (unsigned graph = 0; graph < MAX_GRAPHS; graph++) {
			Nitpicker::Point point = _points[i]._points[graph];
			if (!point.x() && !point.y())
				continue;

			Genode::Color color = _color(graph);
			int value = _points[i]._values[graph];
			point = _apply_data_point(value, i);

			marker(point, _marker_half, color);

			_points[i]._points[graph] = point;
			_points[i]._values[graph] = value;
		}
	}
}

void Graph::_apply_data(int const value, Genode::Color const &color, int entry)
{
	Nitpicker::Point point = _apply_data_point(value, _elements);
	marker(point, _marker_half, color);

	_points[_elements]._points[entry] = point;
	_points[_elements]._values[entry] = value;
}

void Graph::_handle_graph()
{
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
		_nitpicker.framebuffer()->refresh(0, 0, _width, _height);
	}

	/* reset screen check - XXX scrolling would be nice */
	Nitpicker::Point point = _apply_data_point(0, _elements);
	bool scale_update = !point.x() && !point.y();
	if (scale_update) {
		_elements = 0;
		_scale_e = 2;
	}

	unsigned refresh = 0;
	unsigned too_many = 0;

	do {
		if (scale_update) {
			Genode::memset(_ds->local_addr<void>(), 0, _width * _height * sizeof(Pixel_rgb565));
			_init_screen(_elements == 0);
			_replay_data();
			_nitpicker.framebuffer()->refresh(0, 0, _width, _height);
		}

		scale_update = false;
		refresh = 0;
		too_many = 0;
		unsigned scale_above = 0;

		_graph.xml().for_each_sub_node("entry", [&](Genode::Xml_node &node){
			too_many ++;
			if (refresh >= MAX_GRAPHS)
				return;

			Genode::String<6> cpu;
			Genode::String<64> session_label;
			Genode::Trace::Thread_name thread_name;
			unsigned value = 0;

			node.attribute("cpu").value(cpu);
			node.attribute("label").value(session_label);
			node.attribute("thread").value(thread_name);
			node.attribute("value").value(value);

			for (unsigned graph = 0; graph < MAX_GRAPHS; graph++) {
				if (_cpu[graph] != cpu ||
				    _session_labels[graph] != session_label ||
				    _thread_names[graph] != thread_name)
					continue;

				_graph_used[graph] ++;

				Genode::Color color = _color(graph);
				_apply_data(value, color, graph);

				refresh ++;
				too_many --;
				break;
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

	if (too_many && refresh < MAX_GRAPHS) {
		_graph.xml().for_each_sub_node("entry", [&](Genode::Xml_node &node){
			if (refresh >= MAX_GRAPHS)
				return;

			Genode::String<6> cpu;
			Genode::String<64> session_label;
			Genode::Trace::Thread_name thread_name;
			unsigned value = 0;

			node.attribute("cpu").value(cpu);
			node.attribute("label").value(session_label);
			node.attribute("thread").value(thread_name);
			node.attribute("value").value(value);

			for (unsigned graph = 0; graph < MAX_GRAPHS; graph++) {
				if (_cpu[graph] == cpu &&
				    _session_labels[graph] == session_label &&
				    _thread_names[graph] == thread_name)
					return;
			}

			/* not part of our data */

			/* free up an entry depending on recently used scheme */
			unsigned long free_up  = 0;
			unsigned long max_used = _graph_used[free_up];
			for (unsigned graph = 1; graph < MAX_GRAPHS; graph++) {
				if (_graph_used[free_up] > _graph_used[graph])
					free_up = graph;
				else
					max_used = _graph_used[graph];
			}

			/* replace existing entry with the new one */
			_cpu[free_up]            = cpu;
			_session_labels[free_up] = session_label;
			_thread_names[free_up]   = thread_name;
			_graph_used[free_up]     = max_used;

			Genode::Color color = _color(free_up);
			_apply_data(value, color, free_up);

			refresh ++;
		});

		/* reset all used values */
		for (unsigned graph = 1; graph < MAX_GRAPHS; graph++)
			_graph_used[graph]  = 0;
	}

	if (!refresh)
		return;

	unsigned xpos = _apply_data_point(10 /* does not matter value */, _elements).x();
	_nitpicker.framebuffer()->refresh(xpos - _marker_half, 0,
	                                  _marker_half * 2 + 1, _height);

	_elements = (_elements + 1) % (sizeof(_points) / sizeof(_points[0]));
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

			for (unsigned i = 0; i < _elements; i++) {
				for (unsigned j = 0; j < MAX_GRAPHS; j++) {
					if (!_data(i,j).x() && !_data(i,j).y())
						continue;

					if (i == hovered_old && hovered_old_entry >= MAX_GRAPHS)
						hovered_old_entry = j;

					if (_data(i,j).x() - _line_half <= x &&
					    _data(i,j).x() + _line_half >= x)
					{
						hovered_entry  = j;
						hovered        = true;
						_hovered_vline = i;
						last_y         = y;
					}

					if (hovered && hovered_old_entry < MAX_GRAPHS) {
						i = _elements;
						break;
					}
				}
			}
		});
	});

	if (!absevent || (hovered && hovered_entry >= MAX_GRAPHS) ||
	    (hovered_old < _elements && hovered_old_entry >= MAX_GRAPHS))
		return;

	if (!hovered && (hovered_old < _elements)) {
		unsigned const x = _data(hovered_old, hovered_old_entry).x();
		vline(x - _line_half, _black);
		vline(x + _line_half, _black);

		_hovered_vline = ~0U; /* invalid */

		_nitpicker.framebuffer()->refresh(x - _line_half, 0,
		                                  _line_half * 2 + 1, _height);

		/* hiding a view would be nice - destroy and re-create */
		using namespace Nitpicker;
		typedef Session::View_handle View_handle;

		/* HACK - wm does not work properly for destroy_view ... nor to_back XXX */
		Point point(0, _height);
		Rect geometry_text(point, Area { 1, 1 });
		_nitpicker.enqueue<Session::Command::Geometry>(_view_text, geometry_text);

		_nitpicker.enqueue<Session::Command::To_front>(_view, View_handle());

		_nitpicker.execute();
	}

	if (hovered && _hovered_vline != hovered_old) {
		if (hovered_old < _elements) {
			unsigned const x = _data(hovered_old, hovered_old_entry).x();
			vline(x - _line_half, _black);
			vline(x + _line_half, _black);
			_nitpicker.framebuffer()->refresh(x - _line_half, 0,
			                                  _line_half * 2 + 1, _height);
		}
		unsigned const x = _data(_hovered_vline, hovered_entry).x();
		vline(x - _line_half, _white);
		vline(x + _line_half, _white);

		_nitpicker.framebuffer()->refresh(x - _line_half, 0,
		                                  _line_half * 2 + 1, _height);

		memset(_ds->local_addr<Pixel_rgb565>() + _height * _width, 0,
		       (height_mode() - _height) * _width * sizeof(Pixel_rgb565));

		unsigned text_count = 0;
		unsigned max_len    = 0;
		unsigned skipped    = 0;

		/* show infos about data */
		for (unsigned i = 0; i < MAX_GRAPHS; i++) {
			Nitpicker::Point x = _data(_hovered_vline, i);
			if (!x.x() && !x.y()) {
				skipped ++;
				continue;
			}

			Genode::String<128> string(_percent(_value(_hovered_vline, i) / 100,
			                                    _value(_hovered_vline, i) % 100), " ",
			                           _cpu[i].length() < 5 ? " " : "",
			                           _cpu[i], " ",
			                           _thread_names[i], ", ",
			                           _session_labels[i]);

			max_len = Genode::max(string.length(), max_len);

			int const ypos = _height + 5 + (i - skipped) * (_font.height() + 5);
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

		_nitpicker.enqueue<Session::Command::Offset>(_view_text, Point(0, -_height));
		_nitpicker.enqueue<Session::Command::Geometry>(_view_text, geometry_text);
		_nitpicker.enqueue<Session::Command::To_front>(_view_text, _view);
		_nitpicker.execute();
	}
}

void Graph::apply_test_data()
{
	int values_a [] = { 1,   2,  3,  4,  5, 10, 20, 30, 40, 50, 60, 70, 80, 90,
	                    1,   2,  3,  4,  5, 10, 20, 30, 40, 50, 60, 70, 80, 90 };
	int values_b [] = { 95, 78, 75, 30, 20, 12,  5,  1, 10, 20, 30, 10, 10, 5,
	                    95, 78, 75, 30, 20, 12,  5,  1, 10, 20, 30, 10, 10, 5 };
	int values_c [] = { 5 ,  8, 12, 50, 70, 30, 25, 40, 30, 30, 10, 20,  5, 3,
	                    5 ,  8, 12, 50, 70, 30, 25, 40, 30, 30, 10, 20,  5, 3 };

	_elements = sizeof(values_a) / sizeof(values_a[0]);
	if (_elements >= sizeof(_points) / sizeof(_points[0])) {
		Genode::error("too many elements in graph ", _elements);
		return;
	}

	Genode::String<6>          cpu_a("0.0");
	Genode::Trace::Thread_name thread_a("ep");
	Genode::Session_label      session_a("init -> menu_view");
	Genode::String<6>          cpu_b("1.0");
	Genode::Trace::Thread_name thread_b("timer");
	Genode::Session_label      session_b("init -> timer");
	Genode::String<6>          cpu_c("2.0");
	Genode::Trace::Thread_name thread_c("ep");
	Genode::Session_label      session_c("init -> nitpicker");

	_apply_test_data(values_a, _color(0), thread_a, session_a, cpu_a, 0);
	_apply_test_data(values_b, _color(1), thread_b, session_b, cpu_b, 1);
	_apply_test_data(values_c, _color(2), thread_c, session_c, cpu_c, 2);

	_nitpicker.framebuffer()->refresh(0, 0, _width, _height);
}

void Component::construct(Genode::Env &env) { static Graph component(env); }
