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

		enum { MAX_SUBJECTS = 1024 };
		Genode::Trace::Subject_id _subjects[MAX_SUBJECTS];

		enum { MAX_CPUS_X = 16, MAX_CPUS_Y = 1, MAX_ELEMENTS_PER_CPU = 6};

		/* accumulated execution time on all CPUs */
		unsigned long long total [MAX_CPUS_X][MAX_CPUS_Y];

		/* most significant consumer per CPU */
		Entry const * load[MAX_CPUS_X][MAX_CPUS_Y][MAX_ELEMENTS_PER_CPU];

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

				for (unsigned i = 0; i < MAX_ELEMENTS_PER_CPU; i++) {
					if (load[x][y][i])
						continue;

					replace = i;
					break;
				}

				if (replace != NONE) {
					load[x][y][replace] = e;
					continue;
				}

				for (unsigned i = 0; i < MAX_ELEMENTS_PER_CPU; i++) {
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
					for (unsigned k = 0; k < MAX_ELEMENTS_PER_CPU;) {
						if (!load[x][y][k])
							break;

						unsigned i = k;
						for (unsigned j = i; j < MAX_ELEMENTS_PER_CPU; j++) {
							if (!load[x][y][j])
								break;

							if (load[x][y][i]->recent_execution_time
							    < load[x][y][j]->recent_execution_time) {

								Entry const * tmp = load[x][y][j];
								load[x][y][j] = load[x][y][i];
								load[x][y][i] = tmp;

								i++;
								if (i >= MAX_ELEMENTS_PER_CPU || !load[x][y][i])
									break;
							}
						}
						if (i == k)
							k++;
					}
				}
			}

			if (_reconstruct_trace_connection)
				throw Genode::Out_of_ram();
		}

		Genode::String<8> string(unsigned percent, unsigned rest) {
			return Genode::String<8> (percent < 10 ? "  " : (percent < 100 ? " " : ""),
			                          percent, ".", rest < 10 ? "0" : "", rest, "%");
		}

		Genode::String<128> string(Entry const &e, unsigned long long const total,
		                           bool const text = true)
		{
			unsigned const percent = e.recent_execution_time * 100   / total;
			unsigned const rest    = e.recent_execution_time * 10000 / total - (percent * 100);

			Genode::String<8> text_str(text ? string(percent, rest) : "");

			return Genode::String<128>("cpu=", e.info.affinity().xpos(), ".",
			            e.info.affinity().ypos(), " ", text_str, " ",
			            "thread='", e.info.thread_name(), "' "
			            "label='", e.info.session_label(), "'");
		}

		template <typename FN>
		void for_each(FN const &fn) const
		{
			for (unsigned x = 0; x < MAX_CPUS_X; x++) {
				for (unsigned y = 0; y < MAX_CPUS_Y; y++) {
					for (unsigned i = 0; i < MAX_ELEMENTS_PER_CPU; i++) {
						if (!load[x][y][i] || !total[x][y])
							continue;

						fn(load[x][y][i], total[x][y]);
					}
				}
			}
		}

		void top()
		{
			for_each([&] (Entry const * const e, unsigned long long const total) {
				Genode::log(string(*e, total));
			});

			if (load[0][0][0] && load[0][0][0]->recent_execution_time)
				Genode::log("");
		}

		void top(Genode::Reporter::Xml_generator &xml) {
			unsigned vbox_id = 0;
			xml.node("vbox", [&] () {
				xml.attribute("name", vbox_id++);
				for_each([&] (Entry const * const e, unsigned long long const total) {
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
							xml.node("float", [&] () {
								xml.attribute("name", e->id.id * 10 + 3);
								xml.node("label", [&] () {
									xml.attribute("text", string(*e, total, false));
									xml.attribute("color", "#ffffff");
									xml.attribute("align", "left");
								});
							});
						});
					});
				});
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

	Signal_handler<Main> _periodic_handler = {
		_env.ep(), *this, &Main::_handle_period};

	Constructible<Reporter> _reporter { };
	unsigned _reporter_ds_size { 4096 };

	Main(Env &env) : _env(env)
	{
		_config.sigh(_config_handler);
		_handle_config();

		_timer.sigh(_periodic_handler);
	}
};


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

	/* by destructing the session we free up the allocated memory in core */
	if (reconstruct) {
		Genode::warning("re-construct trace session because of out of memory");

		_trace_subject_registry.flush(*_trace, _heap);

		_trace.destruct();
		_trace.construct(_env, TRACE_RAM_QUOTA, ARG_BUFFER_RAM, PARENT_LEVELS);
	}
}


void Component::construct(Genode::Env &env) { static App::Main main(env); }

