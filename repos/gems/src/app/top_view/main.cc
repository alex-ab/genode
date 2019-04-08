/*
 * \brief  Application to show highest CPU consumer per CPU
 * \author Alexander Boettcher
 * \date   2018-06-15
 */

/*
 * Copyright (C) 2018-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <os/reporter.h>
#include <trace_session/connection.h>
#include <timer_session/connection.h>
#include <util/avl_string.h>

#include "button.h"
#include "trace.h"
#include "storage.h"

static constexpr unsigned DIV = 10;
enum SORT_TIME { EC_TIME = 0, SC_TIME = 1};
enum { CHECKBOX_ID_FIRST = 7, CHECKBOX_ID_SECOND = 9 };

using Genode::uint64_t;

struct Subjects
{
	private:

		Genode::Avl_tree<Genode::Avl_string_base> _components { };
		Genode::Avl_tree<Top::Thread>             _threads    { };
		Genode::Trace::Timestamp                  _timestamp  { 0 };

		Top::Component *_lookup_pd(char const * const name)
		{
			Top::Component * component = static_cast<Top::Component *>(_components.first());
			if (!component) return nullptr;

			return (Top::Component *) component->find_by_name(name);
		}

		Top::Thread *_lookup_thread(Genode::Trace::Subject_id const id)
		{
			Top::Thread * thread = _threads.first();
			if (thread)
				thread = thread->find_by_id(id);
			return thread;
		}

		template <typename FN, typename T>
		void _for_each_element(T * const element, FN const &fn) const
		{
			if (!element) return;

			_for_each_element(element->child(T::LEFT), fn);

			fn(*element);

			_for_each_element(element->child(T::RIGHT), fn);
		}

		template <typename FN>
		void for_each_thread(FN const &fn) const
		{
			_for_each_element(_threads.first(), fn);
		}

		template <typename FN>
		void for_each_pd(FN const &fn) const
		{
			_for_each_element(_components.first(), fn);
		}

		enum { MAX_SUBJECTS = 1024 };
		Genode::Trace::Subject_id _subjects[MAX_SUBJECTS];

		enum { MAX_CPUS_X = 32, MAX_CPUS_Y = 2, MAX_ELEMENTS_PER_CPU = 20};

		/* accumulated execution time on all CPUs */
		Genode::uint64_t total_first  [MAX_CPUS_X][MAX_CPUS_Y];
		Genode::uint64_t total_second [MAX_CPUS_X][MAX_CPUS_Y];

		Genode::uint64_t total_cpu_first(Genode::Affinity::Location const &aff) const {
			return total_first[aff.xpos()][aff.ypos()]; }

		Genode::uint64_t total_cpu_second(Genode::Affinity::Location const &aff) const {
			return total_second[aff.xpos()][aff.ypos()]; }

		/* most significant consumer per CPU */
		Top::Thread const * load[MAX_CPUS_X][MAX_CPUS_Y][MAX_ELEMENTS_PER_CPU];

		/* disable report for given CPU */
		bool _cpu_show [MAX_CPUS_X][MAX_CPUS_Y];
		/* state whether cpu is supposed to be available */
		bool _cpu_online [MAX_CPUS_X][MAX_CPUS_Y];

		bool & cpu_show(Genode::Affinity::Location const &loc) {
			return _cpu_show[loc.xpos()][loc.ypos()]; }

		bool & cpu_online(Genode::Affinity::Location const &loc) {
			return _cpu_online[loc.xpos()][loc.ypos()]; }

		unsigned _num_subjects                { 0 };
		unsigned _num_pds                     { 0 };
		unsigned _config_max_elements_per_cpu { 4 };
		unsigned _config_max_pds_per_cpu      { 10 };

		Genode::Trace::Subject_id _hovered_subject { };
		unsigned                  _hovered_sub_id  { 0 };
		Genode::Trace::Subject_id _detailed_view   { };

		Button_state  _button_cpus    { 0, MAX_CPUS_X * MAX_CPUS_Y };
		Button_state  _button_numbers { 2, MAX_ELEMENTS_PER_CPU };
		Button_state  _pd_scroll      { 0, ~0U };
		Button_hub    _button_trace_period { };
		Button_hub    _button_view_period  { };

		Genode::Affinity::Location _button_cpu { };
		Genode::Affinity::Location _last_cpu   { };

		unsigned      _button_number  { 2 };

		unsigned _tracked_threads     { 0 };

		bool _enable_view                { true };

		bool _button_enable_view_hovered { false };
		bool _button_setting             { false };
		bool _button_thread_hovered      { false };
		bool _button_component_hovered   { false };
		bool _button_setting_hovered     { false };
		bool _button_reset_graph_hovered { false };
		bool _button_g_top_all_hovered   { false };
		bool _button_g_top_idle_hovered  { false };
		bool _button_ec_hovered          { false };
		bool _button_sc_hovered          { false };

		bool _trace_top_most             { false };
		bool _trace_top_idle             { false };
		Genode::Affinity::Location _trace_cpu {0,0}; /* XXX */


		bool _reconstruct_trace_connection { false };
		bool _show_second_time             { false };

		enum { THREAD, COMPONENT } _sort { THREAD };

		static constexpr unsigned PD_SCROLL_DOWN = ~0U / DIV;
		static constexpr unsigned PD_SCROLL_UP   = (~0U - DIV) / DIV;
		static constexpr unsigned MAX_SUBJECT_ID =  PD_SCROLL_UP;

		bool _same(Genode::Affinity::Location a, Genode::Affinity::Location b) {
			return a.xpos() == b.xpos() && a.ypos() == b.ypos(); }

		unsigned update_subjects(Genode::Pd_session &pd,
		                         Genode::Trace::Connection &trace)
		{
			Genode::Ram_quota ram_quota;

			do {
				try {
					return trace.subjects(_subjects, MAX_SUBJECTS);
				} catch (Genode::Out_of_ram) {
					trace.upgrade_ram(4096);
				}

				ram_quota = pd.avail_ram();
				_reconstruct_trace_connection = (ram_quota.value < 4 * 4096);

			} while (ram_quota.value >= 2 * 4096);

			return 0;
		}

	public:

		bool trace_top_most() const { return _trace_top_most || _trace_top_idle; }
		bool tracked_threads() const { return _tracked_threads; }

		void period(unsigned long period_trace, unsigned long period_view)
		{
			_button_trace_period.set(period_trace);
			_button_view_period.set(period_view);
		}

		unsigned period_trace() const { return _button_trace_period.value(); }
		unsigned period_view() const { return _button_view_period.value(); }

		void _destroy_thread_object(Top::Thread *thread,
		                            Genode::Trace::Connection &trace,
		                            Genode::Allocator &alloc)
		{
			Top::Component * component = _lookup_pd(thread->session_label());

			trace.free(thread->id());
			_threads.remove(thread);
			Genode::destroy(alloc, thread);

			if (component && !component->_threads.first()) {
				_components.remove(component);
				_num_pds --;
				Genode::destroy(alloc, component);
			}
		}

		void flush(Genode::Trace::Connection &trace, Genode::Allocator &alloc)
		{
			_reconstruct_trace_connection = false;

			while (Top::Thread * const thread = _threads.first()) {
				_destroy_thread_object(thread, trace, alloc);
			}

			/* clear old calculations */
			Genode::memset(total_first , 0, sizeof(total_first));
			Genode::memset(total_second, 0, sizeof(total_second));
			Genode::memset(load, 0, sizeof(load));
		}

		void update(Genode::Pd_session &pd, Genode::Trace::Connection &trace,
		            Genode::Allocator &alloc, SORT_TIME const sort,
		            Genode::Constructible<Top::Storage> &storage)
		{
			constexpr Genode::uint32_t const INVALID_ID = ~0U;

			bool const first_update = !_threads.first();

			_num_subjects = update_subjects(pd, trace);
			_timestamp = Genode::Trace::timestamp();

			if (_num_subjects == MAX_SUBJECTS)
				Genode::error("Not enough memory for all threads - "
				              "calculated utilization is not sane nor "
				              "complete !", _num_subjects);

			/* XXX - right place ?! */
			if (storage.constructed())
				storage->write(Type_a { INVALID_ID /* invalid - means data start */,
				                        Genode::Trace::Execution_time { _timestamp, 0 },
				                        0, 0 });

			/* add and update existing entries */
			for (unsigned i = 0; i < _num_subjects; i++) {

				Genode::Trace::Subject_id const id = _subjects[i];

				Top::Thread * thread = _lookup_thread(id);
				if (!thread) {
					Genode::Trace::Subject_info info = trace.subject_info(id);

					Top::Component * component = _lookup_pd(info.session_label().string());
					if (!component) {
						component = new (alloc) Top::Component(info.session_label().string());
						_components.insert(component);
						_num_pds ++;
					}

					thread = new (alloc) Top::Thread(*component, id, info);
					_threads.insert(thread);

					/* XXX - right place ?! */
					if (storage.constructed())
						storage->write(Type_b { thread->id(),
						                        thread->session_label(),
						                        thread->thread_name(),
						                        unsigned(thread->affinity().xpos()),
						                        unsigned(thread->affinity().ypos()) });
				}

				thread->update(trace.subject_info(id));

				/* remove dead threads which did not run in the last period */
				if (thread->state() == Genode::Trace::Subject_info::DEAD &&
				    !thread->recent_ec_time() && !thread->recent_sc_time())
				{
					_destroy_thread_object(thread, trace, alloc);
				}
			}

			/* clear old calculations */
			Genode::memset(total_first,  0, sizeof(total_first));
			Genode::memset(total_second, 0, sizeof(total_second));
			Genode::memset(load, 0, sizeof(load));

			for_each_thread([&] (Top::Thread &thread) {
				/* collect highest execution time per CPU */
				unsigned const x = thread.affinity().xpos();
				unsigned const y = thread.affinity().ypos();
				if (x >= MAX_CPUS_X || y >= MAX_CPUS_Y) {
					Genode::error("cpu ", thread.affinity().xpos(), ".",
					              thread.affinity().ypos(), " is outside "
					              "supported range ",
					              (int)MAX_CPUS_X, ".", (int)MAX_CPUS_Y);
					return;
				}

				total_first [x][y] += thread.recent_time(sort == EC_TIME);
				total_second[x][y] += thread.recent_time(sort == SC_TIME);

				enum { NONE = ~0U };
				unsigned replace = NONE;

				for (unsigned i = 0; i < _config_max_elements_per_cpu; i++) {
					if (load[x][y][i])
						continue;

					replace = i;
					break;
				}

				if (replace != NONE) {
					load[x][y][replace] = &thread;
					return;
				}

				for (unsigned i = 0; i < _config_max_elements_per_cpu; i++) {
					if (thread.recent_time(sort == EC_TIME)
					    <= load[x][y][i]->recent_time(sort == EC_TIME))
						continue;

					if (replace == NONE) {
						replace = i;
						continue;
					}
					if (load[x][y][replace]->recent_time(sort == EC_TIME)
					    > load[x][y][i]->recent_time(sort == EC_TIME))
						replace = i;
				}

				if (replace != NONE)
					load[x][y][replace] = &thread;
			});

			/* sort */
			for (unsigned x = 0; x < MAX_CPUS_X; x++) {
				for (unsigned y = 0; y < MAX_CPUS_Y; y++) {
					for (unsigned k = 0; k < _config_max_elements_per_cpu;) {
						if (!load[x][y][k])
							break;

						unsigned i = k;
						for (unsigned j = i; j < _config_max_elements_per_cpu; j++) {
							if (!load[x][y][j])
								break;

							if (load[x][y][i]->recent_time(sort == EC_TIME)
							    < load[x][y][j]->recent_time(sort == EC_TIME)) {

								Top::Thread const * tmp = load[x][y][j];
								load[x][y][j] = load[x][y][i];
								load[x][y][i] = tmp;

								i++;
								if (i >= _config_max_elements_per_cpu || !load[x][y][i])
									break;
							}
						}
						if (i == k)
							k++;
					}
				}
			}

			if (storage.constructed()) {
				for (unsigned i = 0; i < _num_subjects; i++) {
					Genode::Trace::Subject_id const id = _subjects[i];
					Top::Thread * thread = _lookup_thread(id);
					if (!thread)
						/* dead threads may be removed from list, see above */
						continue;

					Genode::uint64_t const tf = total_cpu_first(thread->affinity());
					Genode::uint64_t const ts = total_cpu_second(thread->affinity());

					Genode::uint16_t fraq_ec = 0;
					Genode::uint16_t fraq_sc = 0;

					if (sort == EC_TIME)
						fraq_ec = tf ? thread->recent_ec_time() * 10000 / tf : 0;
					else
						fraq_ec = ts ? thread->recent_ec_time() * 10000 / ts : 0;

					if (sort == SC_TIME)
						fraq_sc = tf ? thread->recent_sc_time() * 10000 / tf : 0;
					else
						fraq_sc = ts ? thread->recent_sc_time() * 10000 / ts : 0;

					storage->write(Type_a{thread->id(), thread->execution_time(),
						           fraq_ec, fraq_sc});
				}

				storage->write(Type_c{INVALID_ID});
				storage->write(Type_c{_timestamp});
				storage->write(Type_c{_timestamp >> 32});
//				Genode::log("---- ", Genode::Hex(_timestamp));

				if (_trace_top_most || _trace_top_idle) {
					for_each([&] (Top::Thread const &thread, uint64_t const) {
						int const xpos = thread.affinity().xpos();
						int const ypos = thread.affinity().ypos();
						if (xpos != _trace_cpu.xpos() || ypos != _trace_cpu.ypos()) return;

						if (!_trace_top_idle || !(thread.thread_name() == "idle")) {
							storage->write(Type_c{thread.id()});
						}
					});
				} else {
					for_each_thread([&] (Top::Thread &thread) {
						if (thread.track(sort == EC_TIME)) {
							storage->write(Type_c{thread.id()});
						}
						if (thread.track(sort == SC_TIME)) {
							storage->write(Type_c{thread.id()});
						}
					});
				}
			}

			if (first_update) {
				for (unsigned x = 0; x < MAX_CPUS_X; x++) {
					for (unsigned y = 0; y < MAX_CPUS_Y; y++) {
						if (!total_first[x][y]) continue;

						Genode::Affinity::Location const location(x, y);
						cpu_show(location) = true;
						cpu_online(location) = true;
					}
				}
			}

			/* hacky XXX */
			_show_second_time = total_first[0][0] && total_second[0][0] &&
			                    (total_first[0][0] != total_second[0][0]);

			if (_reconstruct_trace_connection)
				throw Genode::Out_of_ram();
		}

		Genode::String<8> string(unsigned percent, unsigned rest) {
			return Genode::String<8> (percent < 10 ? "  " : (percent < 100 ? " " : ""),
			                          percent, ".", rest < 10 ? "0" : "", rest, "%");
		}

		template <typename FN>
		void for_each(FN const &fn) const
		{
			for (unsigned x = 0; x < MAX_CPUS_X; x++) {
				for (unsigned y = 0; y < MAX_CPUS_Y; y++) {
					if (!_cpu_online[x][y]) continue;

					for (unsigned i = 0; i < _config_max_elements_per_cpu; i++) {
						if (!load[x][y][i]) continue;

						/*
						 * total[x][y] may be 0 in case we remotely request too
						 * quick and no change happened between two requests,
						 * e.g. idle thread sleeping several seconds or
						 * thread with long quantum did not get de-scheduled ...
						 */
						fn(*load[x][y][i], total_first[x][y]);
					}
				}
			}
		}

		template <typename FN>
		void for_each_online_cpu(FN const &fn) const {
			for (unsigned x = 0; x < MAX_CPUS_X; x++) {
				for (unsigned y = 0; y < MAX_CPUS_Y; y++) {
					if (_cpu_online[x][y])
						fn(Genode::Affinity::Location(x, y));
				}
			}
		}

		void top(SORT_TIME const sort)
		{
			for_each([&] (Top::Thread const &thread, unsigned long long const total) {
				unsigned const percent = total ? thread.recent_time(sort == EC_TIME) * 100 / total : 0;
				unsigned const rest    = total ? thread.recent_time(sort == EC_TIME) * 10000 / total - (percent * 100) : 0;

				Genode::log("cpu=", thread.affinity().xpos(), ".",
				                    thread.affinity().ypos(), " ",
				                    string(percent, rest), " ",
				                    "thread='", thread.thread_name(), "'",
				                    " label='", thread.session_label(), "'");
			});

			if (load[0][0][0] && load[0][0][0]->recent_time(sort == EC_TIME))
				Genode::log("");
		}

		void buttons(Genode::Xml_generator &xml, Button_state &state)
		{
			xml.attribute("name", Genode::String<12>("cpusbox", state.current));

			if (state.current > 0) {
				xml.node("button", [&] () {
					xml.attribute("name", "<");
					if (state.prev)
						xml.attribute("hovered","yes");
					xml.node("label", [&] () {
						xml.attribute("text", "...");
					});
				});
			} else
				state.prev = false;

			unsigned i = 0;

			for_each_online_cpu([&] (Genode::Affinity::Location const &loc) {
				i++;
				if (i <= state.current) return;
				if (i > state.current + state.max) return;

				Genode::String<12> cpu_name("cpu", loc.xpos(), ".", loc.ypos());

				xml.node("button", [&] () {
					xml.attribute("name", cpu_name);

					if (_sort == THREAD && cpu_show(loc))
						xml.attribute("selected","yes");
					if (_sort == COMPONENT && _same(_last_cpu, loc))
						xml.attribute("selected","yes");

					if (state.hovered && _same(_button_cpu, loc))
						xml.attribute("hovered","yes");

					xml.node("label", [&] () {
						xml.attribute("text", cpu_name);
					});
				});
			});

			if (i > state.current + state.max) {
				xml.node("button", [&] () {
					xml.attribute("name", ">");
					if (state.next)
						xml.attribute("hovered", "yes");
					xml.node("label", [&] () {
						xml.attribute("text", "...");
					});
				});
			} else {
				state.last = i;
				state.next = false;
			}
		}

		void hub(Genode::Xml_generator &xml, Button_hub &hub, char const *name)
		{
			hub.for_each([&](Button_state &state, unsigned pos) {
				xml.attribute("name", Genode::String<20>("hub-", name, "-", pos));

				Genode::String<12> number(state.current);

				xml.node("button", [&] () {
					xml.attribute("name", Genode::String<20>("hub-", name, "-", pos));
					xml.node("label", [&] () {
						xml.attribute("text", number);
					});
				});
			});
		}

		void numbers(Genode::Xml_generator &xml, Button_state &state)
		{
			xml.attribute("name", Genode::String<12>("numbersbox", state.current));

			if (state.current > state.first) {
				xml.node("button", [&] () {
					xml.attribute("name", "number<");
					if (state.prev)
						xml.attribute("hovered","yes");
					xml.node("label", [&] () {
						xml.attribute("text", "...");
					});
				});
			} else
				state.prev = false;

			unsigned i = 0;

			for (i = state.current; i <= state.last && i < state.current + state.max; i++) {
				Genode::String<12> number(i);

				xml.node("button", [&] () {
					if (_sort == THREAD && _config_max_elements_per_cpu == i)
						xml.attribute("selected","yes");
					if (_sort == COMPONENT && _config_max_pds_per_cpu == i)
						xml.attribute("selected","yes");

					xml.attribute("name", Genode::String<18>("number", number));
					if (state.hovered && _button_number == i)
						xml.attribute("hovered","yes");

					xml.node("label", [&] () {
						xml.attribute("text", number);
					});
				});
			}

			if (i <= state.last) {
				xml.node("button", [&] () {
					xml.attribute("name", "number>");
					if (state.next)
						xml.attribute("hovered","yes");
					xml.node("label", [&] () {
						xml.attribute("text", "...");
					});
				});
			}
		}

		bool hover(Genode::String<12> const button,
		           Genode::String<12> const click,
		           bool const click_valid,
		           Genode::Trace::Subject_id const id,
		           unsigned sub_id,
		           SORT_TIME &sort_time)
		{
			if (click_valid) {

				if (click == "wheel_up" || click == "wheel_down") {
					if (_detailed_view.id) return false;

					if (_button_cpus.hovered) {
						_button_cpus.prev = (click == "wheel_up");
						_button_cpus.next = (click == "wheel_down") && (_button_cpus.current + _button_cpus.max < _button_cpus.last);

						return _button_cpus.advance();
					}

					if (_button_numbers.hovered) {
						_button_numbers.prev = (click == "wheel_up");
						_button_numbers.next = (click == "wheel_down") && (_button_numbers.current + _button_numbers.max < _button_numbers.last);
						return _button_numbers.advance();
					}

					if (_sort == COMPONENT && _hovered_subject.id) {
						_pd_scroll.prev = (click == "wheel_up");
						_pd_scroll.next = (click == "wheel_down") && (_pd_scroll.current + _config_max_pds_per_cpu <= _pd_scroll.last);
						return _pd_scroll.advance();
					}

					return false;
				}

				if (_detailed_view.id) {
					/* checkbox selection */
					if (_hovered_subject.id) {
						Top::Thread * thread = _lookup_thread(_hovered_subject.id);
						if (thread && (_hovered_sub_id == CHECKBOX_ID_FIRST ||
						               _hovered_sub_id == CHECKBOX_ID_SECOND))
						{
							if (_hovered_sub_id == CHECKBOX_ID_FIRST) {
								if (thread->track(sort_time == EC_TIME))
									_tracked_threads --;
								else
									_tracked_threads ++;

								thread->track(sort_time == EC_TIME, !thread->track(sort_time == EC_TIME));
							}
							if (_hovered_sub_id == CHECKBOX_ID_SECOND) {
								if (thread->track(sort_time == SC_TIME))
									_tracked_threads --;
								else
									_tracked_threads ++;

								thread->track(sort_time == SC_TIME, !thread->track(sort_time == SC_TIME));
							}
							return true;
						}
					}
					_detailed_view.id   = 0;
					_button_cpus.reset();
					_button_numbers.reset();
					return true;
				}

				bool update = false;

				if (_button_cpus.hovered) {
					if (_sort == THREAD)
						cpu_show(_button_cpu) = !cpu_show(_button_cpu);
					_last_cpu = _button_cpu;
					update = true;
				}
				if (_hovered_subject.id) {
					_detailed_view = _hovered_subject;
					update = true;
				}
				if (_button_numbers.hovered) {
					if (_sort == THREAD && _button_number <= MAX_ELEMENTS_PER_CPU)
						_config_max_elements_per_cpu = _button_number;
					if (_sort == COMPONENT && _button_number <= MAX_ELEMENTS_PER_CPU)
						_config_max_pds_per_cpu = _button_number;
					update = true;
				}
				if (_button_reset_graph_hovered) {
					for_each_thread([&] (Top::Thread &thread) {
						if (thread.track_ec()) thread.track_ec(false);
						if (thread.track_sc()) thread.track_sc(false);
					});
					_tracked_threads = 0;
					_trace_top_most = false;
					_trace_top_idle = false;
					update = true;
				}
				if (_button_g_top_all_hovered) {
					_trace_top_most = !_trace_top_most;
					if (!_trace_top_most) _trace_top_idle = false;
					update = true;
				}
				if (_button_g_top_idle_hovered) {
					_trace_top_idle = !_trace_top_idle;
					if (_trace_top_idle) _trace_top_most = true;
					update = true;
				}
				if (_button_setting_hovered) {
					_button_setting = !_button_setting;
					update = true;
				}
				if (_button_enable_view_hovered) {
					_enable_view = !_enable_view;
					update = true;
				}
				if (_button_thread_hovered) {
					_sort = THREAD;
					update = true;
				}
				if (_button_component_hovered) {
					_sort = COMPONENT;
					update = true;
				}
				if (_button_ec_hovered) {
					sort_time = EC_TIME;
					update = true;
				}
				if (_button_sc_hovered) {
					sort_time = SC_TIME;
					update = true;
				}
				if (click == "left" && _button_trace_period.update_inc())
					update = true;
				if (click == "right" && _button_trace_period.update_dec())
					update = true;
				if (click == "left" && _button_view_period.update_inc())
					update = true;
				if (click == "right" && _button_view_period.update_dec())
					update = true;

				update = update || _button_cpus.advance();
				update = update || _button_numbers.advance();
				update = update || _pd_scroll.advance();

				return update;
			}

			if (id.id == PD_SCROLL_DOWN || id.id == PD_SCROLL_UP) {
				_pd_scroll.hovered = false;
				_pd_scroll.prev = id.id == PD_SCROLL_UP;
				_pd_scroll.next = id.id == PD_SCROLL_DOWN;
				_hovered_subject = 0;
				_hovered_sub_id  = 0;
			} else {
				_pd_scroll.reset();
				_hovered_subject = id;
				_hovered_sub_id  = sub_id;
			}

			_button_cpus.reset();
			_button_numbers.reset();
			_button_trace_period.reset();
			_button_view_period.reset();
			_button_setting_hovered = false;
			_button_reset_graph_hovered = false;
			_button_g_top_all_hovered = false;
			_button_g_top_idle_hovered = false;
			_button_thread_hovered = false;
			_button_enable_view_hovered = false;
			_button_component_hovered = false;
			_button_ec_hovered = false;
			_button_sc_hovered = false;

			if (button == "")
				return false;

			if (button == "|||") {
				_button_setting_hovered = true;
				return true;
			}
			if (button == "graph_reset") {
				_button_reset_graph_hovered = true;
				return true;
			}
			if (button == "topmost") {
				_button_g_top_all_hovered = true;
				return true;
			}
			if (button == "top_idle") {
				_button_g_top_idle_hovered = true;
				return true;
			}
			if (button == "enable_view") {
				_button_enable_view_hovered = true;
				return true;
			}
			if (button == "threads") {
				_button_thread_hovered = true;
				return true;
			}
			if (button == "components") {
				_button_component_hovered = true;
				return true;
			}
			if (button == "ec") {
				_button_ec_hovered = true;
				return true;
			}
			if (button == "sc") {
				_button_sc_hovered = true;
				return true;
			}

			if (Genode::String<4>(button) == "hub") {
				Genode::String<9> hub_view("hub-view");
				if (Genode::String<9>(button) == hub_view) {
					_button_view_period.for_each([&](Button_state &state, unsigned pos) {
						Genode::String<12> pos_name { hub_view, "-", pos };
						if (Genode::String<12>(button) == pos_name) {
							state.hovered = true;
						}
					});
				}
				Genode::String<10> hub_trace("hub-trace");
				if (Genode::String<10>(button) == hub_trace) {
					_button_trace_period.for_each([&](Button_state &state, unsigned pos) {
						Genode::String<12> pos_name { hub_trace, "-", pos };
						if (Genode::String<12>(button) == pos_name) {
							state.hovered = true;
						}
					});
				}
			}

			if (Genode::String<7>(button) == "number") {
				if (button == "number<")
					_button_numbers.prev = true;
				else if (button == "number>")
					_button_numbers.next = true;
				else {
					for (unsigned i = _button_numbers.first;
					     i <= _button_numbers.last; i++)
					{
						if (Genode::String<10>("number", i) == button) {
							_button_numbers.hovered = true;
							_button_number = i;
							break;
						}
					}
				}
				return _button_numbers.active();
			}

			if (button == "<") {
				_button_cpus.prev    = true;
			} else if (button == ">") {
				_button_cpus.next    = true;
			} else {
				for_each_online_cpu([&] (Genode::Affinity::Location const &loc) {
					Genode::String<12> cpu_name("cpu", loc.xpos(), ".", loc.ypos());
					if (button == cpu_name) {
						_button_cpus.hovered = true;
						_button_cpu = loc;
					}
				});
			}
			return _button_cpus.active() || _button_numbers.active();
		}

		void top(Genode::Reporter::Xml_generator &xml, SORT_TIME const sort, bool const trace_ms)
		{
			if (_detailed_view.id) {
				Top::Thread const * thread = _lookup_thread(_detailed_view);
				if (thread) {
					xml.node("frame", [&] () {
						detail_view(xml, *thread, sort);
					});
					return;
				}
				_detailed_view.id = 0;
			}

			xml.node("frame", [&] () {
				xml.node("hbox", [&] () {
					xml.node("button", [&] () {
						xml.attribute("name", "|||");
						if (_button_setting_hovered) {
							xml.attribute("hovered","yes");
						}
						xml.node("label", [&] () {
							xml.attribute("text", "|||");
						});
					});

					xml.node("vbox", [&] () {
						if (_button_setting) {
							xml.node("hbox", [&] () {
								xml.attribute("name", "aa");

								xml.node("label", [&] () {
									xml.attribute("name", "label2");
									xml.attribute("text", "view:");
								});
								xml.node("button", [&] () {
									xml.attribute("name", "enable_view");
									xml.attribute("style", "checkbox");
									if (_button_enable_view_hovered)
										xml.attribute("hovered","yes");
									if (_enable_view)
										xml.attribute("selected","yes");
									xml.node("label", [&] () {
										xml.attribute("text", "enable");
									});
								});
								xml.node("button", [&] () {
									xml.attribute("name", "threads");
									if (_sort == THREAD)
										xml.attribute("selected","yes");
									if (_button_thread_hovered)
										xml.attribute("hovered","yes");
									xml.node("label", [&] () {
										xml.attribute("text", Genode::String<16>("threads (", _num_subjects,")"));
									});
								});
								xml.node("button", [&] () {
									xml.attribute("name", "components");
									if (_sort == COMPONENT)
										xml.attribute("selected","yes");
									if (_button_component_hovered)
										xml.attribute("hovered","yes");
									xml.node("label", [&] () {
										xml.attribute("text", Genode::String<20>("components (", _num_pds,")"));
									});
								});

								if (_show_second_time) {
									xml.node("label", [&] () {
										xml.attribute("name", "sort");
										xml.attribute("text", "sort:");
									});
									xml.node("button", [&] () {
										xml.attribute("name", "ec");
										if (sort == EC_TIME)
											xml.attribute("selected","yes");
										xml.node("label", [&] () {
											xml.attribute("text", "EC");
										});
									});
									xml.node("button", [&] () {
										xml.attribute("name", "sc");
										if (sort == SC_TIME)
											xml.attribute("selected","yes");
										xml.node("label", [&] () {
											xml.attribute("text", "SC");
										});
									});
								}
#if 0
								xml.node("button", [&] () {
									xml.attribute("name", "prio");
									xml.node("label", [&] () {
										xml.attribute("text", "priority");
									});
								});
#endif
								xml.node("label", [&] () {
									xml.attribute("name", "label_view");
									xml.attribute("text", "view period ms:");
								});
								hub(xml, _button_view_period, "view");
							});

							xml.node("hbox", [&] () {
								xml.attribute("name", "bb");

								xml.node("label", [&] () {
									xml.attribute("name", "label_g");
									xml.attribute("text", "graph:");
								});

								xml.node("button", [&] () {
									xml.attribute("name", "graph_reset");
									xml.attribute("style", "checkbox");
									if (_button_reset_graph_hovered)
										xml.attribute("hovered","yes");
									xml.node("label", [&] () {
										xml.attribute("text", "reset");
									});
								});

								xml.node("button", [&] () {
									xml.attribute("name", "topmost");
									if (_button_g_top_all_hovered)
										xml.attribute("hovered","yes");
									if (_trace_top_most)
										xml.attribute("selected","yes");
									xml.node("label", [&] () {
										xml.attribute("text", "topmost");
									});
								});

								xml.node("button", [&] () {
									xml.attribute("name", "top_idle");
									if (_button_g_top_idle_hovered)
										xml.attribute("hovered","yes");
									if (_trace_top_idle)
										xml.attribute("selected","yes");
									xml.node("label", [&] () {
										xml.attribute("text", "w/o idle");
									});
								});

								if (trace_ms) {
									xml.node("label", [&] () {
										xml.attribute("name", "label_trace");
										xml.attribute("text", "trace period ms:");
									});
									hub(xml, _button_trace_period, "trace");
								}
							});
						}

						if (_enable_view) {
							xml.node("hbox", [&] () {
								xml.attribute("name", "cc");
								if (_button_setting) {
									xml.node("vbox", [&] () {
										buttons(xml, _button_cpus);
									});
									xml.node("vbox", [&] () {
										numbers(xml, _button_numbers);
									});
								}

								if (_sort == THREAD) list_view(xml, sort);
								if (_sort == COMPONENT) list_view_pd(xml, sort);
							});
						}
					});
				});
			});
		}

		void graph(Genode::Reporter::Xml_generator &xml, SORT_TIME const sort)
		{
			if (_trace_top_most || _trace_top_idle) {
				for_each([&] (Top::Thread const &thread, uint64_t t) {
					int const xpos = thread.affinity().xpos();
					int const ypos = thread.affinity().ypos();
					if (xpos != _trace_cpu.xpos() || ypos != _trace_cpu.ypos()) return;

					if (_trace_top_idle && (thread.thread_name() == "idle")) return;

					xml.node("entry", [&]{
						int const xpos = thread.affinity().xpos();
						int const ypos = thread.affinity().ypos();

						Genode::String<12> cpu_name(xpos, ".", ypos, _show_second_time ? (sort == EC_TIME ? " ec" : " sc") : "");
						xml.attribute("cpu", cpu_name);
						xml.attribute("label", thread.session_label());
						xml.attribute("thread", thread.thread_name());
						xml.attribute("id", thread.id().id);
						xml.attribute("tsc", _timestamp);

						xml.attribute("value", t ? (thread.recent_time(sort == EC_TIME) * 10000 / t) : 0 );
					});
				});
				return;
			}

			for_each_thread([&] (Top::Thread &thread) {
				if (thread.track(sort == EC_TIME)) {
					xml.node("entry", [&]{
						int const xpos = thread.affinity().xpos();
						int const ypos = thread.affinity().ypos();

						Genode::String<12> cpu_name(xpos, ".", ypos, _show_second_time ? (sort == EC_TIME ? " ec" : " sc") : "");
						xml.attribute("cpu", cpu_name);
						xml.attribute("label", thread.session_label());
						xml.attribute("thread", thread.thread_name());
						xml.attribute("id", thread.id().id);
						xml.attribute("tsc", _timestamp);

						Genode::uint64_t t = total_cpu_first(thread.affinity());

						xml.attribute("value", t ? (thread.recent_time(sort == EC_TIME) * 10000 / t) : 0 );
					});
				}
				if (thread.track(sort == SC_TIME)) {
					xml.node("entry", [&]{
						int const xpos = thread.affinity().xpos();
						int const ypos = thread.affinity().ypos();

						Genode::String<12> cpu_name(xpos, ".", ypos, _show_second_time ? (sort == SC_TIME ? " ec" : " sc") : "");
						xml.attribute("cpu", cpu_name);
						xml.attribute("label", thread.session_label());
						xml.attribute("thread", thread.thread_name());
						xml.attribute("id", thread.id().id);
						xml.attribute("tsc", _timestamp);

						Genode::uint64_t t = total_cpu_second(thread.affinity());

						xml.attribute("value", t ? (thread.recent_time(sort == SC_TIME) * 10000 / t) : 0 );
					});
				}
			});
		}

		template <typename FN>
		void detail_view_tool(Genode::Xml_generator &xml,
		                      Top::Thread const &entry,
		                      Genode::String<16> name,
		                      unsigned id,
		                      FN const &fn)
		{
			xml.node("vbox", [&] () {
				xml.attribute("name", Genode::String<20>(name, id));

				xml.node("hbox", [&] () {
					xml.attribute("name", name);
					xml.node("label", [&] () {
						xml.attribute("text", name);
						xml.attribute("color", "#ffffff");
						xml.attribute("align", "left");
					});
				});

				entry.for_each_thread_of_pd([&] (Top::Thread &thread) {
					bool left = true;
					Genode::String<64> text(fn(thread, left));

					xml.node("hbox", [&] () {
						xml.attribute("name", thread.id().id * DIV + id);
						xml.attribute("west", "yes");
						xml.node("label", [&] () {
							xml.attribute("text", text);
							xml.attribute("color", "#ffffff");
							xml.attribute("align", left ? "left" : "right");
						});
					});
				});
			});
		}

		void detail_view(Genode::Xml_generator &xml, Top::Thread const &thread,
		                 SORT_TIME const sort)
		{
			xml.node("vbox", [&] () {
				xml.attribute("name", "detail_view");

				xml.node("hbox", [&] () {
					xml.attribute("name", "header");
					xml.node("button", [&] () {
						xml.attribute("name", "<");
						xml.node("label", [&] () {
							xml.attribute("text", "<");
						});
					});
					xml.node("float", [&] () {
						xml.attribute("name", thread.id().id * DIV);
						xml.node("label", [&] () {
							xml.attribute("text", thread.session_label());
							xml.attribute("color", "#ffffff");
							xml.attribute("align", "left");
						});
					});
				});

				xml.node("hbox", [&] () {
					xml.attribute("name", thread.id().id * DIV + 1);
					xml.node("label", [&] () {
						xml.attribute("text", Genode::String<64>("kernel memory: X/Y 4k pages"));
						xml.attribute("color", "#ffffff");
						xml.attribute("align", "left");
					});
				});

				xml.node("hbox", [&] () {
					xml.attribute("name", "list");

					detail_view_tool(xml, thread, Genode::String<16>("cpu "), 2,
						[&] (Top::Thread const &e, bool &) {
							return Genode::String<8>(e.affinity().xpos(), ".", e.affinity().ypos(), " ");
						});

					detail_view_tool(xml, thread, Genode::String<16>("load ", _show_second_time ? (sort == EC_TIME ? "ec " : "sc ") : ""), 3,
						[&] (Top::Thread const &e, bool &left) {
							unsigned long long t = total_first[e.affinity().xpos()][e.affinity().ypos()];
							unsigned const percent = t ? (e.recent_time(sort == EC_TIME) * 100   / t) : 0;
							unsigned const rest    = t ? (e.recent_time(sort == EC_TIME) * 10000 / t - (percent * 100)) : 0;

							left = false;
							return Genode::String<9>(string(percent, rest), " ");
						});

					detail_view_tool(xml, thread, Genode::String<16>("thread "), 4,
						[&] (Top::Thread const &e, bool &) {
							return Genode::String<64>(e.thread_name(), " ");
						});

					detail_view_tool(xml, thread, Genode::String<16>("prio "), 5,
						[&] (Top::Thread const &e, bool &) {
							return Genode::String<4>(e.execution_time().priority);
						});

					detail_view_tool(xml, thread, Genode::String<16>("quantum "), 6,
						[&] (Top::Thread const &e, bool &) {
							return Genode::String<10>(e.execution_time().quantum, "us");
						});

					xml.node("vbox", [&] () {
						xml.attribute("name", "track_first");

						xml.node("hbox", [&] () {
							xml.attribute("name", "track_first");
							xml.node("label", [&] () {
								xml.attribute("text", "");
								xml.attribute("color", "#ffffff");
								xml.attribute("align", "left");
							});
						});

						thread.for_each_thread_of_pd([&] (Top::Thread &check) {
							xml.node("button", [&] () {
								xml.attribute("name", check.id().id * DIV + CHECKBOX_ID_FIRST);
								xml.attribute("style", "checkbox");
								if (check.track(sort == EC_TIME))
									xml.attribute("selected","yes");
								xml.node("hbox", [&] () { });
							});
						});
					});

					if (_show_second_time) {
						detail_view_tool(xml, thread, Genode::String<16>("load ", sort == SC_TIME ? "ec " : "sc "), 8,
							[&] (Top::Thread const &e, bool &left) {
								unsigned long long t = total_second[e.affinity().xpos()][e.affinity().ypos()];
								unsigned const percent = t ? (e.recent_time(sort == SC_TIME) * 100   / t) : 0;
								unsigned const rest    = t ? (e.recent_time(sort == SC_TIME) * 10000 / t - (percent * 100)) : 0;

								left = false;
								return Genode::String<9>(string(percent, rest), " ");
							});

						xml.node("vbox", [&] () {
							xml.attribute("name", "track_second");

							xml.node("hbox", [&] () {
								xml.attribute("name", "track_second");
								xml.node("label", [&] () {
									xml.attribute("text", "");
									xml.attribute("color", "#ffffff");
									xml.attribute("align", "left");
								});
							});

							thread.for_each_thread_of_pd([&] (Top::Thread &check) {
								xml.node("button", [&] () {
									xml.attribute("name", check.id().id * DIV + CHECKBOX_ID_SECOND);
									xml.attribute("style", "checkbox");
									if (check.track(sort == SC_TIME))
										xml.attribute("selected","yes");
									xml.node("hbox", [&] () { });
								});
							});
						});
					}
				});
			});
		}

		template <typename FN>
		void list_view_tool(Genode::Xml_generator &xml,
		                    Genode::String<16> name,
		                    unsigned id,
		                    FN const &fn)
		{
			xml.node("vbox", [&] () {
				xml.attribute("name", Genode::String<20>(name, id));

				xml.node("hbox", [&] () {
					xml.attribute("name", name);
					xml.node("label", [&] () {
						xml.attribute("text", name);
						xml.attribute("color", "#ffffff");
						xml.attribute("align", "left");
					});
				});

				for_each([&] (Top::Thread const &thread, unsigned long long const) {

					if (!cpu_show(thread.affinity()))
						return;

					bool left = true;
					Genode::String<64> text(fn(thread, left));

					xml.node("hbox", [&] () {
						xml.attribute("name", thread.id().id * DIV + id);
						xml.node("label", [&] () {
							xml.attribute("text", text);
							xml.attribute("color", "#ffffff");
							xml.attribute("align", left ? "left" : "right");
						});
					});
				});
			});
		}

		void list_view_bar(Genode::Xml_generator &xml,
		                   Top::Thread const &thread,
		                   unsigned percent, unsigned rest)
		{
			xml.node("float", [&] () {
				xml.attribute("name", thread.id().id * DIV);
				xml.attribute("west", "yes");
				xml.node("hbox", [&] () {
					xml.attribute("name", thread.id().id * DIV + 1);
					xml.node("float", [&] () {
						xml.attribute("name", thread.id().id * DIV + 2);
						xml.attribute("west", "yes");
						xml.node("bar", [&] () {
							if (thread.session_label("kernel")) {
								xml.attribute("color", "#00ff000");
								xml.attribute("textcolor", "#f000f0");
							} else {
								xml.attribute("color", "#ff0000");
								xml.attribute("textcolor", "#ffffff");
							}

							xml.attribute("percent", percent);
							xml.attribute("width", 128);
							xml.attribute("text", string(percent, rest));
						});
					});
				});
			});
		}

		void list_view(Genode::Xml_generator &xml, SORT_TIME const sort)
		{
			xml.node("vbox", [&] () {
				xml.attribute("name", "list_view_load");

				xml.node("hbox", [&] () {
					Genode::String<16> name("load ", _show_second_time ? (sort == EC_TIME ? "ec " : "sc ") : "");
					xml.attribute("name", "load");
					xml.node("label", [&] () {
						xml.attribute("text", name);
						xml.attribute("color", "#ffffff");
						xml.attribute("align", "left");
					});
				});

				for_each([&] (Top::Thread const &thread, unsigned long long const total) {

					if (!cpu_show(thread.affinity()))
						return;

					Genode::uint64_t time = thread.recent_time(sort == EC_TIME);
					unsigned percent = total ? time * 100 / total : 0;
					unsigned rest    = total ? time * 10000 / total - (percent * 100) : 0;

					list_view_bar(xml, thread, percent, rest);
				});
			});

			if (_show_second_time)
			{
				list_view_tool(xml, Genode::String<16>("load ", sort == SC_TIME ? "ec " : "sc "), 2,
					[&] (Top::Thread const &e, bool &left) {
						left = false;

						Genode::uint64_t time = e.recent_time(sort == SC_TIME);
						Genode::uint64_t total = total_second[e.affinity().xpos()][e.affinity().ypos()];
						unsigned percent = total ? (time * 100 / total) : 0;
						unsigned rest    = total ? (time * 10000 / total - (percent * 100)) : 0;

						return string(percent, rest);
					});
			}

			list_view_tool(xml, Genode::String<16>("cpu "), 3,
				[&] (Top::Thread const &e, bool &left) {
					left = false;
					return Genode::String<8>(e.affinity().xpos(), ".", e.affinity().ypos(), " ");
				});

			list_view_tool(xml, Genode::String<16>("thread "), 4,
				[&] (Top::Thread const &e, bool &) {
					return Genode::String<64>(e.thread_name(), " ");
				});

			list_view_tool(xml, Genode::String<16>("label"), 5,
				[&] (Top::Thread const &e, bool &) {
					return Genode::String<64>(e.session_label());
				});
		}

		void list_view_pd(Genode::Xml_generator &xml, SORT_TIME const sort)
		{
			Genode::String<18> label("load cpu", _last_cpu.xpos(), ".", _last_cpu.ypos(), _show_second_time ? (sort == EC_TIME ? " ec " : " sc ") : " ");
			list_view_pd_tool(xml, "list_view_load", "load", label.string(),
			                  [&] (Top::Component const &, Top::Thread const &thread)
			{
				Genode::uint64_t   time   = 0;

				thread.for_each_thread_of_pd([&] (Top::Thread &t) {
					if (_same(t.affinity(), _last_cpu))
						time += t.recent_time(sort == EC_TIME);
				});

				Genode::uint64_t max = total_first[_last_cpu.xpos()][_last_cpu.ypos()];

				unsigned percent = max ? (time * 100 / max) : 0;
				unsigned rest    = max ? (time * 10000 / max - (percent * 100)) : 0;

				list_view_bar(xml, thread, percent, rest);
			});

			if (_show_second_time) {

				Genode::String<18> label("load cpu", _last_cpu.xpos(), ".", _last_cpu.ypos(), " ", sort == SC_TIME ? "ec " : "sc ");
				list_view_pd_tool(xml, "list_view_load_sc", "load", label.string(),
				                  [&] (Top::Component const &, Top::Thread const &thread)
				{
					Genode::uint64_t   time   = 0;

					thread.for_each_thread_of_pd([&] (Top::Thread &t) {
						if (_same(t.affinity(), _last_cpu))
							time += t.recent_time(sort == SC_TIME);
					});

					Genode::uint64_t max = total_second[_last_cpu.xpos()][_last_cpu.ypos()];

					unsigned percent = max ? (time * 100 / max) : 0;
					unsigned rest    = max ? (time * 10000 / max - (percent * 100)) : 0;

					list_view_bar(xml, thread, percent, rest);
				});
			}

			list_view_pd_tool(xml, "components", "components", "components ",
			                  [&] (Top::Component const &component,
			                       Top::Thread const &thread)
			{
				xml.node("hbox", [&] () {
					xml.attribute("name", thread.id().id * DIV + 3);
					xml.node("label", [&] () {
						xml.attribute("text", component.name());
						xml.attribute("color", "#ffffff");
						xml.attribute("align", "left");
					});
				});
			});
		}

		template <typename FN>
		void list_view_pd_tool(Genode::Xml_generator &xml,
		                       char const * const name,
		                       char const * const attribute,
		                       char const * const attribute_label,
		                       FN const &fn)
		{
			unsigned max_pds = _config_max_pds_per_cpu;

			xml.node("vbox", [&] () {
				xml.attribute("name", name);

				xml.node("hbox", [&] () {
					xml.attribute("name", attribute);
					xml.node("label", [&] () {
						xml.attribute("text", attribute_label);
						xml.attribute("color", "#ffffff");
					});
				});

				unsigned pd_count = 0;

				if (pd_count < _pd_scroll.current) {
					xml.node("hbox", [&] () {
						xml.attribute("name", PD_SCROLL_UP * DIV);
						xml.node("label", [&] () {
							xml.attribute("text", "...");
							xml.attribute("color", "#ffffff");
						});
					});
				}

				for_each_pd([&] (Genode::Avl_string_base const &base) {

					pd_count ++;
					if (pd_count - 1 < _pd_scroll.current || pd_count > _pd_scroll.current + max_pds)
						return;

					Top::Component const &component = static_cast<Top::Component const &>(base);
					Top::Thread const *thread = component._threads.first();
					if (!thread) {
						Genode::warning("component without any thread ?");
						return;
					}

					fn(component, *thread);
				});

				if (pd_count > _pd_scroll.current + max_pds) {
					xml.node("hbox", [&] () {
						xml.attribute("name", PD_SCROLL_DOWN * DIV);
						xml.node("label", [&] () {
							xml.attribute("text", "...");
							xml.attribute("color", "#ffffff");
						});
					});
				}
			});
		}
};


namespace App {

	struct Main;
	using namespace Genode;
}


struct App::Main
{
	static unsigned long _default_period_ms() { return 5000; }

	enum {
		TRACE_RAM_QUOTA = 10 * 4096,
		ARG_BUFFER_RAM  = 32 * 1024,
		PARENT_LEVELS   = 0
	};

	Env &_env;

	Reconstructible<Trace::Connection> _trace { _env, TRACE_RAM_QUOTA,
	                                            ARG_BUFFER_RAM, PARENT_LEVELS };

	unsigned long          _period_trace  { _default_period_ms() };
	unsigned long          _period_view   { _default_period_ms() };
	bool                   _use_log       { true };
	bool                   _empty_graph   { true };
	bool                   _updated_trace { false };
	SORT_TIME              _sort          { EC_TIME };
	Attached_rom_dataspace _config        { _env, "config" };
	Timer::Connection      _timer         { _env };
	Heap                   _heap          { _env.ram(), _env.rm() };
	Subjects               _subjects      { };
	unsigned               _dialog_size   { 2 * 4096 };
	unsigned               _graph_size    { 4096 };


	void _handle_config();
	void _handle_trace(Duration);
	void _handle_view(Duration);
	void _handle_hover();
	void _generate_report();

	Signal_handler<Main> _config_handler = {
		_env.ep(), *this, &Main::_handle_config};

	Constructible<Timer::Periodic_timeout<Main>> _timeout_trace { };
	Constructible<Timer::Periodic_timeout<Main>> _timeout_view { };

	Signal_handler<Main> _hover_handler = {
		_env.ep(), *this, &Main::_handle_hover};

	Constructible<Reporter>               _reporter { };
	Constructible<Reporter>               _reporter_graph { };
	Constructible<Attached_rom_dataspace> _hover { };
	Constructible<Top::Storage>           _storage { };

	Main(Env &env) : _env(env)
	{
		_config.sigh(_config_handler);
		_handle_config();
	}
};

template <typename T>
static T _attribute_value(Genode::Xml_node node, char const *attr_name)
{
	return node.attribute_value(attr_name, T{});
}

template <typename T, typename... ARGS>
static T _attribute_value(Genode::Xml_node node, char const *sub_node_type, ARGS... args)
{
	if (!node.has_sub_node(sub_node_type))
		return T{};

	return _attribute_value<T>(node.sub_node(sub_node_type), args...);
}

/**
 * Query attribute value from XML sub nodd
 *
 * The list of arguments except for the last one refer to XML path into the
 * XML structure. The last argument denotes the queried attribute name.
 */
template <typename T, typename... ARGS>
static T query_attribute(Genode::Xml_node node, ARGS &&... args)
{
	return _attribute_value<T>(node, args...);
}

void App::Main::_handle_hover()
{
	/* reconfigure trace period time */
	unsigned long period_trace = _subjects.period_trace();
	unsigned long period_view = _subjects.period_view();

	if ((period_trace != _period_trace) || (period_view != _period_view)) {
		if (period_trace == 0)
			period_trace = 1;
		if (period_view < 100)
			period_view = 100;
		if (period_trace >= period_view)
			period_view = period_trace;

		if (_period_trace != period_trace) {
			_period_trace = period_trace;
			_timeout_trace.destruct();
			_timeout_trace.construct(_timer, *this, &Main::_handle_trace, Microseconds(_period_trace * 1000));
		}
		if (_period_view != period_view) {
			_period_view = period_view;

			/* if storage is off we did not construct the additional timer */
			if (_timeout_view.constructed()) {
				_timeout_view.destruct();
				_timeout_view.construct(_timer, *this, &Main::_handle_view, Microseconds(_period_view * 1000));
			}
		}

		_subjects.period(period_trace, period_view);
	}

	_hover->update();

	if (!_hover->valid())
		return;

	Xml_node const hover = _hover->xml();

	typedef String<12> Button;
	Button button = query_attribute<Button>(hover, "dialog", "frame",
	                                        "hbox", "button", "name");
	if (button == "") {
		button = query_attribute<Button>(hover, "dialog", "frame", "hbox",
		                                 "vbox", "hbox", "button", "name");
	}
	if (button == "") {
		button = query_attribute<Button>(hover, "dialog", "frame", "hbox",
		                                 "vbox", "hbox", "vbox", "button", "name");
	}

	bool click_valid = false;
	Button click = query_attribute<Button>(hover, "button", "left");
	if (click == "yes") {
		click = "left";
		click_valid = true;
	} else {
		click = query_attribute<Button>(hover, "button", "right");
		if (click == "yes") {
			click = "right";
			click_valid = true;
		} else {
			long y = query_attribute<long>(hover, "button", "wheel");
			click_valid = y;
			if (y < 0) click = "wheel_down";
			if (y > 0) click = "wheel_up";
		}
	}

	Genode::Trace::Subject_id id {
		query_attribute<unsigned>(hover, "dialog", "frame", "hbox", "vbox",
		                          "hbox", "vbox", "hbox", "name") / DIV};
	unsigned sub_id = 0;

	if (!id.id) {
		sub_id = query_attribute<unsigned>(hover, "dialog", "frame", "vbox",
		                                  "hbox", "vbox", "button", "name");
		id = sub_id / 10;
		sub_id = sub_id % 10;
	}

	if (_subjects.hover(button, click, click_valid, id, sub_id, _sort))
		_generate_report();
}

void App::Main::_handle_config()
{
	_config.update();

	_period_view = _config.xml().attribute_value("view_ms", _default_period_ms());
	_period_trace = _config.xml().attribute_value("trace_ms", _period_view);
	_use_log = _config.xml().attribute_value("log", true);
	bool const store = _config.xml().attribute_value("store", false);

	String<8> ec_sc(_config.xml().attribute_value("sort_time", String<8>("ec")));
	if (ec_sc == "ec")
		_sort = EC_TIME;
	else
		_sort = SC_TIME;

	if (store && !_storage.constructed())
		_storage.construct(_env);
	if (!store && _storage.constructed())
		_storage.destruct();

	if (_timeout_trace.constructed())
		_timeout_trace.destruct();
	if (_timeout_view.constructed())
		_timeout_view.destruct();

	_timeout_trace.construct(_timer, *this, &Main::_handle_trace, Microseconds(_period_trace * 1000));
	if (_storage.constructed())
		_timeout_view.construct(_timer, *this, &Main::_handle_view, Microseconds(_period_view * 1000));
	else
		_period_view = _period_trace;

	_subjects.period(_period_trace, _period_view);

	if (_config.xml().attribute_value("report", false)) {
		if (!_reporter.constructed()) {
			_reporter.construct(_env, "dialog", "dialog", _dialog_size);
			_reporter->enabled(true);
		}
		if (!_hover.constructed()) {
			_hover.construct(_env, "hover");
			_hover->sigh(_hover_handler);
		}
		if (!_reporter_graph.constructed()) {
			_reporter_graph.construct(_env, "graph", "graph", _graph_size);
			_reporter_graph->enabled(true);
		}
	} else {
		if (_reporter.constructed())
			_reporter.destruct();
	}
}


void App::Main::_handle_view(Duration)
{
	if (!_updated_trace) return;

	_updated_trace = false;

	/* show most significant consumers */
	if (_use_log)
		_subjects.top(_sort);

	if (_storage.constructed())
		_storage->force_data_flush();

	_generate_report();
}

void App::Main::_handle_trace(Duration time)
{
	bool reconstruct = false;

	/* update subject information */
	try {
		_subjects.update(_env.pd(), *_trace, _heap, _sort, _storage);
	} catch (Genode::Out_of_ram) {
		reconstruct = true;
	}

	_updated_trace = true;

	if (_period_trace == _period_view)
		_handle_view(time);

	/* by destructing the session we free up the allocated memory in core */
	if (reconstruct) {
		Genode::warning("re-construct trace session because of out of memory");

		_subjects.flush(*_trace, _heap);

		_trace.destruct();
		_trace.construct(_env, TRACE_RAM_QUOTA, ARG_BUFFER_RAM, PARENT_LEVELS);
	}
}

void App::Main::_generate_report()
{
	if (_reporter.constructed()) {
		unsigned retry = 0;

		do {
			try {
				Reporter::Xml_generator xml(*_reporter, [&] () {
					_subjects.top(xml, _sort, _storage.constructed()); });

				retry = 0;
			} catch (Genode::Xml_generator::Buffer_exceeded) {
				if (++retry % 5 == 0)
					Genode::warning(retry, ". attempt to extend dialog report size");

				_dialog_size += 4096;
				_reporter.destruct();
				_reporter.construct(_env, "dialog", "dialog", _dialog_size);
				_reporter->enabled(true);
			}
		} while (retry);
	}

	bool const show_graph = !_empty_graph || _subjects.tracked_threads() || _subjects.trace_top_most();
	if (_reporter_graph.constructed() && show_graph) {
		unsigned retry = 0;

		do {
			try {
				Reporter::Xml_generator xml(*_reporter_graph, [&] () {
					_subjects.graph(xml, _sort);
				});

				retry = 0;
			} catch (Genode::Xml_generator::Buffer_exceeded) {
				if (++retry % 5 == 0)
					Genode::warning(retry, ". attempt to extend graph report size");

				_graph_size += 4096;
				_reporter_graph.destruct();
				_reporter_graph.construct(_env, "graph", "graph", _graph_size);
				_reporter_graph->enabled(true);
			}
		} while (retry);
	}

	_empty_graph = !_subjects.tracked_threads() && !_subjects.trace_top_most();
}


void Component::construct(Genode::Env &env) { static App::Main main(env); }
