/*
 * \brief  Report server that writes reports to file-systems
 * \author Emery Hemingway
 * \author Norman Feske
 * \date   2017-05-19
 */

/*
 * Copyright (C) 2017-2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <os/path.h>
#include <os/vfs.h>
#include <report_session/report_session.h>
#include <root/component.h>
#include <base/attached_rom_dataspace.h>
#include <base/attached_ram_dataspace.h>
#include <base/session_label.h>
#include <base/heap.h>
#include <base/component.h>
#include <util/arg_string.h>

namespace Fs_report {

	using namespace Genode;
	using namespace Report;

	class  Session_component;
	class  Root;
	struct Main;

	using Path = Genode::Path<Session_label::capacity()>;

	static void create_parent_dir(Directory &dir, Path const &child)
	{
		Path parent = child;
		parent.strip_last_element();
		if (parent == "/")
			return;

		dir.create_sub_directory(parent.string());
	}
}


struct Fs_report::Session_component : Rpc_object<Report::Session>
{
	Directory &_root_dir;

	Attached_ram_dataspace _ds;

	Path _path { };

	Session_component(Env &env, Directory &root_dir,
	                  Session_label const &label, size_t buffer_size)
	:
		_root_dir(root_dir),
		_ds(env.ram(), env.rm(), buffer_size),
		_path(path_from_label<Path>(label.string()))
	{
		create_parent_dir(_root_dir, _path);
	}

	~Session_component() { }

	Dataspace_capability dataspace() override { return _ds.cap(); }

	void submit(size_t const length) override
	{
		Span const bytes { _ds.local_addr<char>(), min(length, _ds.size()) };

		try {
			New_file dst { _root_dir, _path.string() };

			if (dst.append(bytes) != New_file::Append_result::OK)
				error("failed to write '", _path,"'");

		} catch (New_file::Create_failed) {
			error("failed to create '", _path,"'");
		}
	}

	void response_sigh(Signal_context_capability) override { }

	size_t obtain_response() override { return 0; }
};


class Fs_report::Root : public Root_component<Session_component>
{
	private:

		Env &_env;

		Heap _heap { &_env.ram(), &_env.rm() };

		Attached_rom_dataspace _config_rom { _env, "config" };

		Root_directory _root_dir = _config_rom.node().with_sub_node("vfs",
			[&] (Node const &config) -> Root_directory {
				return { _env, _heap, config }; },
			[&] () -> Root_directory {
				error("VFS not configured");
				return { _env, _heap, Node() }; });

		Signal_handler<Root> _config_dispatcher {
			_env.ep(), *this, &Root::_config_update };

		void _config_update()
		{
			_config_rom.update();

			_config_rom.node().with_optional_sub_node("vfs", [&] (Node const &node) {
				_root_dir.apply_config(node); });
		}

	protected:

		Create_result _create_session(const char *args) override
		{
			using namespace Genode;

			/* read label from session arguments */
			Session_label const label = label_from_args(args);

			/* read RAM donation from session arguments */
			size_t const ram_quota =
				Arg_string::find_arg(args, "ram_quota").aligned_size();
			/* read report buffer size from session arguments */
			size_t const buffer_size =
				Arg_string::find_arg(args, "buffer_size").aligned_size();

			if (buffer_size > ram_quota) {
				error("insufficient 'ram_quota' from '", label, "' "
				      "got ", ram_quota, ", need ", buffer_size);
				throw Insufficient_ram_quota();
			}

			return *new (md_alloc())
				Session_component(_env, _root_dir, label, buffer_size);
		}

	public:

		Root(Env &env, Allocator &md_alloc)
		:
			Root_component<Session_component>(env.ep(), md_alloc),
			_env(env)
		{ }
};


struct Fs_report::Main
{
	Env &_env;

	Sliced_heap _sliced_heap { _env.ram(), _env.rm() };

	Root _root { _env, _sliced_heap };

	Main(Env &env) : _env(env)
	{
		env.parent().announce(env.ep().manage(_root));
	}
};

void Component::construct(Genode::Env &env)
{
	static Fs_report::Main main(env);
}
