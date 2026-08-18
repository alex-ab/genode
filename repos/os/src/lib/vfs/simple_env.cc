/*
 * \brief   File-system factory implementation
 * \author  Norman Feske
 * \date    2014-04-09
 */

/*
 * Copyright (C) 2014-2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */


/* Genode includes */
#include <vfs/simple_env.h>
#include <base/shared_object.h>

/* supported builtin file systems */
#include <block_file_system.h>
#include <fs_file_system.h>
#include <inline_file_system.h>
#include <log_file_system.h>
#include <null_file_system.h>
#include <ram_file_system.h>
#include <rom_file_system.h>
#include <rtc_file_system.h>
#include <symlink_file_system.h>
#include <tar_file_system.h>
#include <terminal_file_system.h>
#include <zero_file_system.h>
#include <vfs/dir_file_system.h>


/**
 * Name of factory provided by a VFS shared library
 */
static char const *_factory_symbol() { return "vfs_file_system_factory"; }


struct Genode::Vfs::Simple_env::Factory::Entry_base : File_system::Factory,
                                                      private List<Entry_base>::Element
{
	friend class Genode::List<Entry_base>;

	using Genode::List<Entry_base>::Element::next;

	using Fs_type_name = String<128>;

	Fs_type_name name;

	Entry_base(Fs_type_name const &name) : name(name) { }

	bool matches(Node const &node) const { return node.has_type(name.string()); }
};


template <typename FILE_SYSTEM>
struct Genode::Vfs::Simple_env::Factory::Builtin_entry : Entry_base
{
	Builtin_entry() : Entry_base(FILE_SYSTEM::name()) { }

	Vfs::File_system *create(Vfs::Env &env, Parent_fs &parent_fs, Node const &node) override {
		return new (env.alloc()) FILE_SYSTEM(env, parent_fs, node); }
};


struct Genode::Vfs::Simple_env::Factory::External_entry : Entry_base
{
	File_system::Factory &_fs_factory;

	External_entry(Fs_type_name  const &name, File_system::Factory &fs_factory)
	:
		Entry_base(name), _fs_factory(fs_factory)
	{ }

	File_system *create(Vfs::Env &env, Parent_fs &parent_fs, Node const &config) override
	{
		return _fs_factory.create(env, parent_fs, config);
	}
};


/**
 * Add builtin File_system type
 */
template <typename FILE_SYSTEM>
void Genode::Vfs::Simple_env::Factory::_add_builtin_fs()
{
	_list.insert(new (&_md_alloc) Builtin_entry<FILE_SYSTEM>());
}


static Genode::Vfs::File_system::Factory *load_factory(Genode::Vfs::Env &env,
                                                       auto const &lib_name)
{
	using namespace Genode;

	Shared_object *shared_object = nullptr;

	try {
		shared_object = new (env.alloc())
			Shared_object(env.env(), env.alloc(), lib_name.string(),
			              Shared_object::BIND_LAZY,
			              Shared_object::DONT_KEEP);

		typedef Vfs::File_system::Factory *(*Query_fn)();

		Query_fn query_fn = shared_object->lookup<Query_fn>(_factory_symbol());

		return query_fn();

	} catch (Shared_object::Invalid_rom_module) {
		warning("could not open '", lib_name, "'");
		return nullptr;

	} catch (Shared_object::Invalid_symbol) {
		warning("could not find symbol '", Cstring(_factory_symbol()),
		        "' in '", lib_name, "'");

		destroy(env.alloc(), shared_object);
		return nullptr;
	}
}


/**
 * Try to load external File_system::Factory provider
 */
bool Genode::Vfs::Simple_env::Factory::_probe_external_factory(Vfs::Env &env,
                                                               Node const &node)
{
	String<128> const lib_name { "vfs_", node.type(), ".lib.so" };

	Vfs::File_system::Factory *factory_ptr = load_factory(env, lib_name);
	if (!factory_ptr)
		return false;

	_list.insert(new (env.alloc()) External_entry(node.type().string(), *factory_ptr));
	return true;
}


/**
 * Create and return a new file-system
 */
Genode::Vfs::File_system *
Genode::Vfs::Simple_env::Factory::create(Vfs::Env   &env,
                                         Parent_fs  &parent_fs,
                                         Node const &node)
{
	auto try_create = [&] () -> Vfs::File_system *
	{
		for (Entry_base *e = _list.first(); e; e = e->next())
			if (e->matches(node))
				try { return e->create(env, parent_fs, node); }
				catch (...) { return nullptr; }

		return nullptr;
	};

	/* try if type is handled by the currently registered fs types */
	if (Vfs::File_system *fs = try_create())
		return fs;

	/* if the builtin fails, do not try loading an external */

	/* probe for file system implementation available as shared lib */
	if (_probe_external_factory(env, node))
		/* try again with the new file system type loaded */
		if (Vfs::File_system *fs = try_create())
			return fs;

	return nullptr;
}


/**
 * Register an additional factory for new file-system type
 */
void Genode::Vfs::Simple_env::Factory::extend(char const *name, File_system::Factory &factory)
{
	_list.insert(new (&_md_alloc) External_entry(name, factory));
}


/**
 * Constructor
 */
Genode::Vfs::Simple_env::Factory::Factory(Allocator &alloc)
:
	_md_alloc(alloc)
{
	_add_builtin_fs<Vfs_tar     ::File_system>();
	_add_builtin_fs<Vfs_fs      ::File_system>();
	_add_builtin_fs<Vfs_terminal::File_system>();
	_add_builtin_fs<Vfs_null    ::File_system>();
	_add_builtin_fs<Vfs_zero    ::File_system>();
	_add_builtin_fs<Vfs_block   ::File_system>();
	_add_builtin_fs<Vfs_log     ::File_system>();
	_add_builtin_fs<Vfs_rom     ::File_system>();
	_add_builtin_fs<Vfs_inline  ::File_system>();
	_add_builtin_fs<Vfs_rtc     ::File_system>();
	_add_builtin_fs<Vfs_ram     ::File_system>();
	_add_builtin_fs<Vfs_symlink ::File_system>();
	_add_builtin_fs<Dir_file_system>();
}
