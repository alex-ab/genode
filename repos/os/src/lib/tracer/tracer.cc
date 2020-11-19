/*
 * \brief  Component-locale TRACE monitor for debugging purposes
 * \author Josef Soentgen
 * \date   2020-10-29
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/registry.h>
#include <trace/tracer.h>
#include <trace_session/connection.h>

/* local includes */
#include "trace_buffer.h"


using namespace Genode;

struct Local_tracer
{
	Env                  &_env;
	Heap                  _alloc;
	Tracer::Config const  _config;

	Trace::Connection _trace;

	using String = Genode::String<64>;

	String           _policy_module { "null" };
	Trace::Policy_id _policy_id { };

	struct Failed                    : Genode::Exception { };
	struct Argument_buffer_too_small : Genode::Exception { };

	struct Local_id
	{
		Constructible<Trace_buffer> trace_buffer { };
		Tracer::Id const id;

		Local_id(Tracer::Id const id)
		: id { id } { }

		virtual ~Local_id() { }
	};
	using Reg_id = Genode::Registered<Local_id>;
	Genode::Registry<Reg_id> _id_registry { };

	Local_tracer(Env &env, Tracer::Config const config)
	:
		_env    { env },
		_alloc  { _env.ram(), _env.rm() },
		_config { config },
		_trace  { env, config.session_quota.value,
		          config.arg_buffer_quota.value, 0 }
	{
		try {
			Rom_connection policy_rom(env, _policy_module.string());
			Rom_dataspace_capability policy_module_rom_ds = policy_rom.dataspace();

			size_t rom_size = Dataspace_client(policy_module_rom_ds).size();

			_policy_id = _trace.alloc_policy(rom_size);
			Dataspace_capability ds_cap = _trace.policy(_policy_id);

			if (ds_cap.valid()) {
				void *ram = env.rm().attach(ds_cap);
				void *rom = env.rm().attach(policy_module_rom_ds);
				memcpy(ram, rom, rom_size);

				env.rm().detach(ram);
				env.rm().detach(rom);
			}
		} catch (...) {
			error("could not load module '", _policy_module, "'");
			throw Failed();
		}
	}

	Tracer::Lookup_result lookup_subject(char const *label, char const *thread_name)
	{
		Trace::Subject_id _sub_id { };
		bool found = false;

		auto enable_tracing = [&] (Trace::Subject_id id,
		                                    Trace::Subject_info info) {

			if (   info.session_label() != label
			    || info.thread_name()   != thread_name) {
				return;
			}

			if (found) {
				warning("skip matching subject: ", id.id,
				        " - already found: ", _sub_id.id);
				return;
			}

			_sub_id = id;
			found = true;
			log("Found '", info.session_label(), "' '", info.thread_name(),
			    "' id: ", _sub_id.id);
		};

		Trace::Session_client::For_each_subject_info_result const res =
			_trace.for_each_subject_info(enable_tracing);

		/*
		 * On the off chance that there are indeed excactly as many
		 * subjects as will fit into the argument buffer the exception
		 * is misleading but we cannot detect this.
		 */
		if (res.count == res.limit) {
			Genode::error("argument buffer probably too small");
			throw Argument_buffer_too_small();
		}

		Tracer::Id new_id { .value = 0 };

		if (found) {
			new_id = { .value = _sub_id.id };
			new (_alloc) Reg_id(_id_registry, new_id);
		}

		return found ? Tracer::Lookup_result { .id = new_id, .valid = true }
		             : Tracer::Lookup_result { .id = new_id, .valid = false };
	}

	void resume_tracing(Tracer::Id const id)
	{
		bool valid_id = false;
		_id_registry.for_each([&] (Reg_id &rid) {
			if (id.value != rid.id.value) { return; }
			valid_id = true;

			try {
				if (!rid.trace_buffer.constructed()) {
					_trace.trace(rid.id.value, _policy_id, _config.trace_buffer_quota.value);
					Dataspace_capability ds_cap = _trace.buffer(rid.id.value);
					if (!ds_cap.valid()) {
						Genode::error("trace buffer capability invalid");
						throw Failed();
					}
					rid.trace_buffer.construct(_env.rm(), ds_cap);
				} else {
					_trace.resume(rid.id.value);
				}
			} catch (Trace::Source_is_dead) {
				error("source is dead");
				throw Failed();
			}
		});

		if (!valid_id) {
			error("invalid id");
			throw Failed();
		}
	}

	void pause_tracing(Tracer::Id const id)
	{
		bool valid_id = false;
		_id_registry.for_each([&] (Reg_id &rid) {
			if (id.value != rid.id.value) { return; }
			valid_id = true;

			try {
				_trace.pause(rid.id.value);
			} catch (Trace::Source_is_dead) {
				error("source is dead");
				throw Failed();
			}
		});

		if (!valid_id) {
			error("invalid id");
			throw Failed();
		}
	}

	void dump_trace_buffer(Tracer::Id const id)
	{
		bool valid_id = false;
		_id_registry.for_each([&] (Reg_id &rid) {
			if (id.value != rid.id.value) { return; }
			valid_id = true;

			unsigned const wc = rid.trace_buffer->wrap_count();
			if (wc) {
				Genode::warning("TRACEBUFFER ", rid.id.value, " WRAPPED: ", wc);
			}

			using E = Trace::Buffer::Entry;
			rid.trace_buffer->for_each_new_entry([&] (E const &e) {

				if (e.length() == 0) {
					return false;
				}

				char const *data = e.data();
				bool const ending_lf = (data[e.length()-1] == '\n');
				Genode::log("TDUMP: ", Genode::Cstring(e.data(), e.length() - ending_lf));

				return true;
			});
		});

		if (!valid_id) {
			error("invalid id");
			throw Failed();
		}
	}

#if 0
	template <typename FN>
	void dump_trace_buffer(Tracer::Id const id, FN const &fn)
	{
		using E = Trace::Buffer::Entry;
		_trace_buffer->for_each_new_entry([&] (E const &e) {

			if (e.length() == 0) {
				return false;
			}

			Tracer::Entry const entry { .data = e.data(), .length = e.length() };

			fn(entry);

			return true;
		});
	}
#endif
};


static Genode::Constructible<Local_tracer> _tracer { };

struct Tracer_not_initialized : Genode::Exception { };


void Tracer::init(Env &env, Tracer::Config const cfg)
{
	if (_tracer.constructed()) {
		Genode::warning("tracer already initialized");
		return;
	}

	_tracer.construct(env, cfg);
}


Tracer::Lookup_result Tracer::lookup_subject(char const *label, char const *thread)
{
	if (!_tracer.constructed()) {
		Genode::warning("tracer not initialized");
		throw Tracer_not_initialized();
	}

	return _tracer->lookup_subject(label, thread);
}


void Tracer::resume_tracing(Tracer::Id const id)
{
	if (!_tracer.constructed()) {
		Genode::warning("tracer not initialized");
		throw Tracer_not_initialized();
	}

	_tracer->resume_tracing(id);
}


void Tracer::pause_tracing(Tracer::Id const id)
{
	if (!_tracer.constructed()) {
		Genode::warning("tracer not initialized");
		throw Tracer_not_initialized();
	}

	_tracer->pause_tracing(id);
}


void Tracer::dump_trace_buffer(Tracer::Id const id)
{
	if (!_tracer.constructed()) {
		Genode::warning("tracer not initialized");
		throw Tracer_not_initialized();
	}

	_tracer->dump_trace_buffer(id);
}
