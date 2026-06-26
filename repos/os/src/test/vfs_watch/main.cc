/*
 * \brief  Test for the file-watching mechanism of the VFS
 * \author Norman Feske
 * \date   2026-06-08
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/component.h>
#include <base/heap.h>
#include <base/attached_rom_dataspace.h>
#include <os/vfs.h>

namespace Test {

	using namespace Genode;

	struct Main;
}


struct Test::Main
{
	Env &_env;

	Heap _heap { _env.ram(), _env.rm() };

	Attached_rom_dataspace _config { _env, "config" };

	Root_directory _root = _config.node().with_sub_node("vfs",
		[&] (Node const &config) -> Root_directory {
			return { _env, _heap, config }; },
		[&] () -> Root_directory {
			error("VFS not configured");
			return { _env, _heap, Node() }; });

	Attached_rom_dataspace _plan { _env, "plan.hid" };

	Signal_handler<Main> _plan_handler { _env.ep(), *this, &Main::_handle_plan };

	struct Step { unsigned v; };

	Step _curr { };
	Step const _num_steps { _plan.node().num_sub_nodes() };

	struct Watched : Noncopyable
	{
		Main &_main;

		Directory::Path const _path;

		unsigned _triggered = 0;

		Watch_handler<Watched> _handler;

		void _handle()
		{
			if (!_main._watch_trigger_expected) {
				error("unexpected watch notification");
				return;
			}

			_triggered++;
			_main._plan_handler.local_submit();
		}

		Watched(Main &main, Directory::Path const &path)
		:
			_main(main), _path(path),
			_handler(_main._env.ep(), _main._root, path, *this, &Watched::_handle)
		{ }

		unsigned observed = 0;
	};

	Constructible<Watched> _x { }, _y { }, _z { };

	bool _watch_trigger_expected = false;

	void _next_step()
	{
		_curr.v++;
		if (_curr.v == _num_steps.v) {
			log("all steps done.");
			_env.parent().exit(0);
		} else {
			_plan_handler.local_submit();
		}
	}

	void _with_curr_step(auto const &fn)
	{
		_plan.node().with_sub_node(_curr.v,
			[&] (Node const &node) { fn(node); },
			[&] { warning("no further step to execute"); });
	}

	void _handle_plan()
	{
		_with_curr_step([&] (Node const &node) { _handle_step(node); });
	}

	void _handle_step(Node const &step)
	{
		using Content = String<200>;
		using Path = Directory::Path;

		auto name_attr = [&] { return step.attribute_value("name", Path()); };

		log("step: '", step, "'");

		auto handle_expect_trigger = [&] (auto &watched)
		{
			if (watched.constructed() && watched->_triggered > watched->observed) {
				watched->observed = watched->_triggered;
				_next_step();
			}
		};

		if (step.type() == "new_file") {
			New_file file(_root, name_attr());
			Content { Node::Quoted_content(step) }.with_span([&] (Span const &s) {
				file.append(s); });
			_next_step();

		} else if (step.type() == "append") {
			Append_file file(_root, name_attr());
			Content { Node::Quoted_content(step) }.with_span([&] (Span const &s) {
				file.append(s); });
			_next_step();

		} else if (step.type() == "rename") {
			_root.rename(name_attr(), step.attribute_value("to", Path()));
			_next_step();

		} else if (step.type() == "unlink") {
			_root.unlink(name_attr());
			_next_step();

		} else if (step.type() == "watch_x") {
			_x.construct(*this, name_attr());
			_next_step();

		} else if (step.type() == "watch_y") {
			_y.construct(*this, name_attr());
			_next_step();

		} else if (step.type() == "watch_z") {
			_z.construct(*this, name_attr());
			_next_step();

		} else if (step.type() == "expect_trigger_x") {
			handle_expect_trigger(_x);

		} else if (step.type() == "expect_trigger_y") {
			handle_expect_trigger(_y);

		} else if (step.type() == "expect_trigger_z") {
			handle_expect_trigger(_z);

		} else if (step.type() == "expect_content") {

			bool match = false;
			Content { Node::Quoted_content(step) }.with_span([&] (Span const &expected) {
				File_content const content { _heap, _root, name_attr(), { 1000 } };
				content.bytes([&] (char const *start, size_t num_bytes) {
					match = (expected.equals({ start, num_bytes}));
					if (!match) {
						warning("expected: '", Cstring(expected.start, expected.num_bytes), "'");
						warning("got:      '", Cstring(start, num_bytes), "'");
					}
				});
			});
			if (match)
				_next_step();

		} else if (step.type() == "expect_missing") {
			if (!_root.file_exists(name_attr()))
				_next_step();

		} else {
			error("invalid test step: '", step, "'");
		}

		_with_curr_step([&] (Node const &step) {
			_watch_trigger_expected = step.type() == "expect_trigger_x"
			                       || step.type() == "expect_trigger_y"
			                       || step.type() == "expect_trigger_z"; });
	}

	Main(Env &env) : _env(env)
	{
		_plan_handler.local_submit();
	}
};


void Component::construct(Genode::Env &env) { static Test::Main main(env); }

