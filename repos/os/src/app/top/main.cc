/*
 * \brief  Application to show highest CPU consumer per CPU
 * \author Norman Feske
 *         Alexander Boettcher
 * \date   2015-06-15
 */

/*
 * Copyright (C) 2015-2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <trace_session/connection.h>
#include <timer_session/connection.h>
#include <base/component.h>
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <os/reporter.h>

struct Button_state
{
	unsigned  const first;
	unsigned  const last;
	unsigned  current;
	unsigned  max     { 6 };
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
};

struct Trace_subject_registry
{
	private:

		struct Entry : Genode::List<Entry>::Element
		{
			Genode::Trace::Subject_id const id;

			Genode::Trace::Subject_info info { };

			/**
			 * Execution time during the last period
			 */
			unsigned long long recent_execution_time = 0;

			bool track { false };

			Entry(Genode::Trace::Subject_id id) : id(id) { }

			void update(Genode::Trace::Subject_info const &new_info)
			{
				if (new_info.execution_time().value < info.execution_time().value)
					recent_execution_time = 0;
				else
					recent_execution_time = new_info.execution_time().value -
				                            info.execution_time().value;

				info = new_info;
			}
		};

		Genode::List<Entry> _entries { };

		Entry *_lookup(Genode::Trace::Subject_id const id)
		{
			for (Entry *e = _entries.first(); e; e = e->next())
				if (e->id == id)
					return e;

			return nullptr;
		}

		template <typename FN>
		void for_each_entry(FN const &fn) const
		{
			for (Entry const * e = _entries.first(); e; e = e->next())
				fn(e);
		}

		enum { MAX_SUBJECTS = 1024 };
		Genode::Trace::Subject_id _subjects[MAX_SUBJECTS];

		enum { MAX_CPUS_X = 32, MAX_CPUS_Y = 2, MAX_ELEMENTS_PER_CPU = 12};

		/* accumulated execution time on all CPUs */
		unsigned long long total [MAX_CPUS_X][MAX_CPUS_Y];

		/* most significant consumer per CPU */
		Entry const * load[MAX_CPUS_X][MAX_CPUS_Y][MAX_ELEMENTS_PER_CPU];

		/* disable report for given CPU */
		bool _cpu_enable [MAX_CPUS_X][MAX_CPUS_Y];

		bool & cpu_enable(Genode::Affinity::Location const &loc) {
			return _cpu_enable[loc.xpos()][loc.ypos()]; }

		unsigned _config_max_elements_per_cpu { 6 };

		Genode::Trace::Subject_id _hovered_subject { };
		Genode::Trace::Subject_id _detailed_view   { };

		Button_state  _button_cpus    { 0, MAX_CPUS_X * MAX_CPUS_Y };
		Button_state  _button_numbers { 2, MAX_ELEMENTS_PER_CPU };

		Genode::Affinity::Location _button_cpu { };
		unsigned      _button_number  { 2 };

		bool _button_setting { false };
		bool _button_setting_hovered { false };

		bool _reconstruct_trace_connection = false;

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

		void flush(Genode::Trace::Connection &trace, Genode::Allocator &alloc)
		{
			_reconstruct_trace_connection = false;

			while (Entry * const e = _entries.first()) {
					trace.free(e->id);
					_entries.remove(e);
					Genode::destroy(alloc, e);
			}
		}

		void update(Genode::Pd_session &pd, Genode::Trace::Connection &trace,
		            Genode::Allocator &alloc)
		{
			bool const first_update = !_entries.first();

			unsigned const num_subjects = update_subjects(pd, trace);

			if (num_subjects == MAX_SUBJECTS)
				Genode::error("Not enough memory for all threads - "
				              "calculated utilization is not sane nor "
				              "complete !", num_subjects);

			/* add and update existing entries */
			for (unsigned i = 0; i < num_subjects; i++) {

				Genode::Trace::Subject_id const id = _subjects[i];

				Entry *e = _lookup(id);
				if (!e) {
					e = new (alloc) Entry(id);
					_entries.insert(e);
				}

				e->update(trace.subject_info(id));

				/* remove dead threads which did not run in the last period */
				if (e->info.state() == Genode::Trace::Subject_info::DEAD &&
				    !e->recent_execution_time) {

					trace.free(e->id);
					_entries.remove(e);
					Genode::destroy(alloc, e);
				}
			}

			/* clear old calculations */
			Genode::memset(total, 0, sizeof(total));
			Genode::memset(load, 0, sizeof(load));

			for (Entry const *e = _entries.first(); e; e = e->next()) {

				/* collect highest execution time per CPU */
				unsigned const x = e->info.affinity().xpos();
				unsigned const y = e->info.affinity().ypos();
				if (x >= MAX_CPUS_X || y >= MAX_CPUS_Y) {
					Genode::error("cpu ", e->info.affinity().xpos(), ".",
					              e->info.affinity().ypos(), " is outside "
					              "supported range ",
					              (int)MAX_CPUS_X, ".", (int)MAX_CPUS_Y);
					continue;
				}

				total[x][y] += e->recent_execution_time;

				enum { NONE = ~0U };
				unsigned replace = NONE;

				for (unsigned i = 0; i < _config_max_elements_per_cpu; i++) {
					if (load[x][y][i])
						continue;

					replace = i;
					break;
				}

				if (replace != NONE) {
					load[x][y][replace] = e;
					continue;
				}

				for (unsigned i = 0; i < _config_max_elements_per_cpu; i++) {
					if (e->recent_execution_time
					    <= load[x][y][i]->recent_execution_time)
						continue;

					if (replace == NONE) {
						replace = i;
						continue;
					}
					if (load[x][y][replace]->recent_execution_time
					    > load[x][y][i]->recent_execution_time)
						replace = i;
				}

				if (replace != NONE)
					load[x][y][replace] = e;
			}

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

							if (load[x][y][i]->recent_execution_time
							    < load[x][y][j]->recent_execution_time) {

								Entry const * tmp = load[x][y][j];
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

			if (first_update) {
				unsigned i = 0;
				per_cpu_one([&] (Entry const * const e, unsigned long long) {
					if (i >= _button_cpus.max / 2) return;

					cpu_enable(e->info.affinity()) = true;
					i++;
				});
			}

			if (_reconstruct_trace_connection)
				throw Genode::Out_of_ram();
		}

		Genode::String<8> string(unsigned percent, unsigned rest) {
			return Genode::String<8> (percent < 10 ? "  " : (percent < 100 ? " " : ""),
			                          percent, ".", rest < 10 ? "0" : "", rest, "%");
		}

		Genode::String<128> string(Entry const &e,
		                           unsigned long long const total)
		{
			return string(e, total, true, true);
		}

		Genode::String<128> string(Entry const &e,
		                           unsigned long long const total,
		                           bool const text, bool const label)
		{
			unsigned const percent = e.recent_execution_time * 100   / total;
			unsigned const rest    = e.recent_execution_time * 10000 / total - (percent * 100);

			Genode::String<8> text_str(text ? string(percent, rest) : "");

			return Genode::String<128>("cpu=", e.info.affinity().xpos(), ".",
			                           e.info.affinity().ypos(), " ",
			                           text_str, " ",
			                           "thread='", e.info.thread_name(), "'",
			                           label ? " label='" : "",
			                           label ? e.info.session_label(): "",
			                           label ? "'" : "");
		}

		template <typename FN>
		void for_each(FN const &fn, bool all) const
		{
			for (unsigned x = 0; x < MAX_CPUS_X; x++) {
				for (unsigned y = 0; y < MAX_CPUS_Y; y++) {
					for (unsigned i = 0; i < _config_max_elements_per_cpu; i++) {
						if (!load[x][y][i] || !total[x][y])
							continue;

						fn(load[x][y][i], total[x][y]);

						if (!all)
							break;
					}
				}
			}
		}

		template <typename FN>
		void for_each(FN const &fn) const { for_each(fn, true); }

		template <typename FN>
		void per_cpu_one(FN const &fn) const { for_each(fn, false); }

		void top()
		{
			for_each([&] (Entry const * const e, unsigned long long const total) {
				Genode::log(string(*e, total));
			});

			if (load[0][0][0] && load[0][0][0]->recent_execution_time)
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
						xml.attribute("text", "<");
					});
				});
			} else
				state.prev = false;

			unsigned i = 0;

			per_cpu_one([&] (Entry const * const e, unsigned long long) {
				i++;
				if (i <= state.current) return;
				if (i > state.current + state.max) return;

				int const xpos = e->info.affinity().xpos();
				int const ypos = e->info.affinity().ypos();

				Genode::String<12> cpu_name("cpu", xpos, ".", ypos);

				xml.node("button", [&] () {
					xml.attribute("name", cpu_name);

					if (cpu_enable(e->info.affinity()))
						xml.attribute("selected","yes");

					if (state.hovered &&
						_button_cpu.xpos() == e->info.affinity().xpos() &&
						_button_cpu.ypos() == e->info.affinity().ypos())
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
						xml.attribute("text", ">");
					});
				});
			} else
				state.next = false;
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
						xml.attribute("text", "<");
					});
				});
			} else
				state.prev = false;

			unsigned i = 0;

			for (i = state.current; i <= state.last && i < state.current + state.max; i++) {
				Genode::String<12> number(i);

				xml.node("button", [&] () {
					if (_config_max_elements_per_cpu == i)
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
						xml.attribute("text", ">");
					});
				});
			}
		}

		bool hover(Genode::String<12> const button,
		           Genode::String<12> const click,
		           Genode::Trace::Subject_id const id)
		{
			if (click == "yes") {
				if (_detailed_view.id) {
					/* checkbox selection */
					if (_hovered_subject.id) {
						Entry * entry = _lookup(_hovered_subject.id);
						if (entry) {
							entry->track = !entry->track;
							return true;
						}
					}
					_detailed_view.id   = 0;
					_button_cpus.reset();
					_button_numbers.reset();
					return true;
				}

				bool update = false;
				//Genode::log("click next=", _button_cpus.next, " prev=", _button_cpus.prev, " button=", _button_cpus.hovered);

				if (_button_cpus.hovered) {
					cpu_enable(_button_cpu) = !cpu_enable(_button_cpu);
					update = true;
				}
				if (_hovered_subject.id) {
					_detailed_view = _hovered_subject;
					update = true;
				}
				if (_button_numbers.hovered) {
					if (_button_number <= MAX_ELEMENTS_PER_CPU)
						_config_max_elements_per_cpu = _button_number;
					update = true;
				}
				if(_button_setting_hovered) {
					_button_setting = !_button_setting;
					update = true;
				}

				update = update || _button_cpus.advance();
				update = update || _button_numbers.advance();

				return update;
			}

			_hovered_subject = id;

			_button_cpus.reset();
			_button_numbers.reset();
			_button_setting_hovered = false;

			if (button == "")
				return false;

			if (button == "|||") {
				_button_setting_hovered = true;
				return true;
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
				per_cpu_one([&] (Entry const * const e, unsigned long long) {
					Genode::Affinity::Location location = e->info.affinity();
					Genode::String<12> cpu_name("cpu", location.xpos(), ".", location.ypos());
					if (button == cpu_name) {
						_button_cpus.hovered = true;
						_button_cpu = location;
					}
				});
			}
			return _button_cpus.active() || _button_numbers.active();
		}

		void top(Genode::Reporter::Xml_generator &xml)
		{
			if (_detailed_view.id) {
				Entry const * entry = _lookup(_detailed_view);
				if (entry) {
					xml.node("frame", [&] () {
						detail_view(xml, *entry);
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
					if (_button_setting) {
						xml.node("vbox", [&] () {
							buttons(xml, _button_cpus);
						});
						xml.node("vbox", [&] () {
							numbers(xml, _button_numbers);
						});
					}
					list_view(xml);
				});
			});
		}

		template <typename FN>
		void detail_view_tool(Genode::Xml_generator &xml,
		                      Entry const &entry,
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

				for_each_entry([&] (Entry const * const e) {
					if (e->info.session_label() != entry.info.session_label())
						return;

					bool left = true;
					Genode::String<64> text(fn(*e, left));

					xml.node("hbox", [&] () {
						xml.attribute("name", e->id.id * 10 + id);
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

		void detail_view(Genode::Xml_generator &xml, Entry const & entry)
		{
			unsigned vbox_id = 0;

			xml.node("vbox", [&] () {
				xml.attribute("name", vbox_id++);

				xml.node("hbox", [&] () {
					xml.attribute("name", "header");
					xml.node("button", [&] () {
						xml.attribute("name", "<");
						xml.node("label", [&] () {
							xml.attribute("text", "<");
						});
					});
					xml.node("float", [&] () {
						xml.attribute("name", entry.id.id * 10);
						xml.node("label", [&] () {
							xml.attribute("text", entry.info.session_label());
							xml.attribute("color", "#ffffff");
							xml.attribute("align", "left");
						});
					});
				});

				xml.node("hbox", [&] () {
					xml.attribute("name", entry.id.id * 10 + 1);
					xml.node("label", [&] () {
						xml.attribute("text", Genode::String<64>("kernel memory: X/Y 4k pages"));
						xml.attribute("color", "#ffffff");
						xml.attribute("align", "left");
					});
				});

				xml.node("hbox", [&] () {
					xml.attribute("name", "list");

					detail_view_tool(xml, entry, Genode::String<16>("cpu "), 2,
						[&] (Entry const &e, bool &) {
							return Genode::String<8>(e.info.affinity().xpos(), ".", e.info.affinity().ypos(), " ");
						});

					detail_view_tool(xml, entry, Genode::String<16>("load "), 3,
						[&] (Entry const &e, bool &left) {
							unsigned long long t = total[e.info.affinity().xpos()][e.info.affinity().ypos()];
							unsigned const percent = t ? (e.recent_execution_time * 100   / t) : 0;
							unsigned const rest    = t ? (e.recent_execution_time * 10000 / t - (percent * 100)) : 0;

							left = false;
							return Genode::String<9>(string(percent, rest), " ");
						});

					detail_view_tool(xml, entry, Genode::String<16>("thread "), 4,
						[&] (Entry const &e, bool &) {
							return Genode::String<64>(e.info.thread_name(), " ");
						});

					detail_view_tool(xml, entry, Genode::String<16>("prio "), 5,
						[&] (Entry const &, bool &) {
							return Genode::String<64>("XX");
						});

					detail_view_tool(xml, entry, Genode::String<16>("quantum "), 6,
						[&] (Entry const &, bool &) {
							return Genode::String<64>("10000us");
						});

					xml.node("vbox", [&] () {
						xml.attribute("name", "track");

						xml.node("hbox", [&] () {
							xml.attribute("name", "track");
							xml.node("label", [&] () {
								xml.attribute("text", "");
								xml.attribute("color", "#ffffff");
								xml.attribute("align", "left");
							});
						});

						for_each_entry([&] (Entry const * const e) {
							if (e->info.session_label() != entry.info.session_label())
								return;

							xml.node("button", [&] () {
								xml.attribute("name", e->id.id * 10 + 9);
								xml.attribute("style", "checkbox");
								if (e->track)
									xml.attribute("selected","yes");
								xml.node("hbox", [&] () { });
							});
						});
					});
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

				for_each([&] (Entry const * const e, unsigned long long const) {

					if (!cpu_enable(e->info.affinity()))
						return;

					bool left = true;
					Genode::String<64> text(fn(*e, left));

					xml.node("hbox", [&] () {
						xml.attribute("name", e->id.id * 10 + id);
						xml.node("label", [&] () {
							xml.attribute("text", text);
							xml.attribute("color", "#ffffff");
							xml.attribute("align", left ? "left" : "right");
						});
					});
				});
			});
		}

		void list_view(Genode::Xml_generator &xml)
		{
			unsigned vbox_id = 0;
			xml.node("vbox", [&] () {
				xml.attribute("name", vbox_id++);

				xml.node("hbox", [&] () {
					xml.attribute("name", "load");
					xml.node("label", [&] () {
						xml.attribute("text", "load ");
						xml.attribute("color", "#ffffff");
						xml.attribute("align", "left");
					});
				});

				for_each([&] (Entry const * const e, unsigned long long const total) {

					if (!cpu_enable(e->info.affinity()))
						return;

					unsigned percent = e->recent_execution_time * 100 / total;
					unsigned rest    = e->recent_execution_time * 10000 / total - (percent * 100);

					xml.node("float", [&] () {
						xml.attribute("name", e->id.id * 10);
						xml.attribute("west", "yes");
						xml.node("hbox", [&] () {
							xml.attribute("name", e->id.id * 10 + 1);
							xml.node("float", [&] () {
								xml.attribute("name", e->id.id * 10 + 2);
								xml.attribute("west", "yes");
								xml.node("bar", [&] () {
									if (e->info.session_label() == "kernel") {
										xml.attribute("color", "#00ff000");
										xml.attribute("textcolor", "#f000f0");
									} else {
										xml.attribute("color", "#ff0000");
										xml.attribute("textcolor", "#ffffff");
									}

									xml.attribute("percent", percent);
									xml.attribute("width", 128);
									xml.attribute("height", 16);
									xml.attribute("text", string(percent, rest));
								});
							});
						});
					});
				});
			});

			list_view_tool(xml, Genode::String<16>("cpu "), 3,
				[&] (Entry const &e, bool &left) {
					left = false;
					return Genode::String<8>(e.info.affinity().xpos(), ".", e.info.affinity().ypos(), " ");
				});

			list_view_tool(xml, Genode::String<16>("thread "), 4,
				[&] (Entry const &e, bool &) {
					return Genode::String<64>(e.info.thread_name(), " ");
				});

			list_view_tool(xml, Genode::String<16>("label"), 5,
				[&] (Entry const &e, bool &) {
					return Genode::String<64>(e.info.session_label());
				});
		}
};


namespace App {

	struct Main;
	using namespace Genode;
}


struct App::Main
{
	Env &_env;

	enum {
		TRACE_RAM_QUOTA = 10 * 4096,
		ARG_BUFFER_RAM  = 32 * 1024,
		PARENT_LEVELS   = 0
	};

	Reconstructible<Trace::Connection> _trace { _env, TRACE_RAM_QUOTA,
	                                            ARG_BUFFER_RAM, PARENT_LEVELS };

	static unsigned long _default_period_ms() { return 5000; }

	unsigned long _period_ms = _default_period_ms();
	bool          _use_log   = true;

	Attached_rom_dataspace _config { _env, "config" };

	Timer::Connection _timer { _env };

	Heap _heap { _env.ram(), _env.rm() };

	Trace_subject_registry _trace_subject_registry { };

	void _handle_config();

	Signal_handler<Main> _config_handler = {
		_env.ep(), *this, &Main::_handle_config};

	void _handle_period();
	void _generate_report();

	Signal_handler<Main> _periodic_handler = {
		_env.ep(), *this, &Main::_handle_period};

	Constructible<Reporter> _reporter { };
	Constructible<Attached_rom_dataspace> _hover { };

	void _handle_hover();

	Signal_handler<Main> _hover_handler = {
		_env.ep(), *this, &Main::_handle_hover};

	unsigned _reporter_ds_size { 4096 };

	Main(Env &env) : _env(env)
	{
		_config.sigh(_config_handler);
		_handle_config();

		_timer.sigh(_periodic_handler);
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
	_hover->update();

	Xml_node const hover = _hover->xml();

	typedef String<12> Button;
	Button button = query_attribute<Button>(hover, "dialog", "frame",
	                                        "hbox", "vbox", "button",
	                                        "name");
	if (button == "") {
		button = query_attribute<Button>(hover, "dialog", "frame",
		                                 "hbox", "button", "name");
	}

	Button const click = query_attribute<Button>(hover, "button", "left");

	Genode::Trace::Subject_id id {
		query_attribute<unsigned>(hover, "dialog", "frame", "hbox", "vbox",
		                          "hbox", "name")/10};

	if (!id.id) {
		id = query_attribute<unsigned>(hover, "dialog", "frame", "vbox",
		                               "hbox", "vbox", "button", "name") / 10;
	}

	if (_trace_subject_registry.hover(button, click, id) && click == "yes")
		_generate_report();
}

void App::Main::_handle_config()
{
	_config.update();

	_period_ms = _config.xml().attribute_value("period_ms", _default_period_ms());
	_use_log   = _config.xml().attribute_value("log", true);

	if (_config.xml().attribute_value("report", false)) {
		if (!_reporter.constructed()) {
			_reporter.construct(_env, "dialog", "dialog", _reporter_ds_size);
			_reporter->enabled(true);
		}
		if (!_hover.constructed()) {
			_hover.construct(_env, "hover");
			_hover->sigh(_hover_handler);
		}
	} else {
		if (_reporter.constructed())
			_reporter.destruct();
	}

	_timer.trigger_periodic(1000*_period_ms);

}


void App::Main::_handle_period()
{
	bool reconstruct = false;

	/* update subject information */
	try {
		_trace_subject_registry.update(_env.pd(), *_trace, _heap);
	} catch (Genode::Out_of_ram) {
		reconstruct = true;
	}

	/* show most significant consumers */
	if (_use_log)
		_trace_subject_registry.top();

	_generate_report();

	/* by destructing the session we free up the allocated memory in core */
	if (reconstruct) {
		Genode::warning("re-construct trace session because of out of memory");

		_trace_subject_registry.flush(*_trace, _heap);

		_trace.destruct();
		_trace.construct(_env, TRACE_RAM_QUOTA, ARG_BUFFER_RAM, PARENT_LEVELS);
	}
}

void App::Main::_generate_report()
{
	if (_reporter.constructed()) {
		bool retry = false;

		do {
			try {
				Reporter::Xml_generator xml(*_reporter, [&] () {
					_trace_subject_registry.top(xml); });

				retry = false;
			} catch (Genode::Xml_generator::Buffer_exceeded) {
				/* give up, after one retry */
				if (retry)
					break;

				_reporter_ds_size += 4096;
				_reporter.destruct();
				_reporter.construct(_env, "dialog", "dialog", _reporter_ds_size);
				_reporter->enabled(true);
				retry = true;
			}
		} while (retry);
	}
}


void Component::construct(Genode::Env &env) { static App::Main main(env); }

