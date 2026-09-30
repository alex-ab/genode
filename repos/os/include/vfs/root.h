/*
 * \brief  VFS instance
 * \author Norman Feske
 * \date   2026-08-18
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__ROOT_H_
#define _INCLUDE__VFS__ROOT_H_

#include <vfs/dir_file_system.h>
#include <vfs/env.h>

namespace Genode::Vfs { struct Root; }


class Genode::Vfs::Root : public Env, private Env::Io, private Env::User
{
	private:

		struct Factory : File_system::Factory
		{
			Allocator &_md_alloc;

			struct Entry_base;
			struct External_entry;
			template <typename> struct Builtin_entry;

			List<Entry_base> _list { };

			template <typename> void _add_builtin_fs();
			bool _probe_external_factory(Env &, Node const &);

			Factory(Allocator &alloc);

			Instance::Attempt create(Env &, Parent_fs &, Node const &) override;

			void _free(Instance &) override
			{
				/*
				 * The 'Result' of 'create' should always refer to a 'Factory'
				 * of a VFS 'File_system'.
				 */
				warning("unexpected call to Vfs::Root::Factory::_free");
			}

			/**
			 * Register an additional factory for new file-system type
			 *
			 * \name     name of file-system type
			 * \factory  factory to create instances of this file-system type
			 */
			void extend(char const *name, File_system::Factory &factory);
		};

		Genode::Env &_env;
		Allocator   &_alloc;
		Env::User   &_user;

		using Deferred_wakeups = Remote_io::Deferred_wakeups;

		Deferred_wakeups _deferred_wakeups { };

		File_handles  _file_handles  { };
		Dir_handles   _dir_handles   { };
		Watch_handles _watch_handles { };

		Factory _fs_factory { _alloc };

		struct Root_parent_fs : Parent_fs
		{
			Root &_root;

			void notify_watchers(Span const &path) override
			{
				_root._watch_handles.for_each([&] (Watch_handle const &handle) {
					handle.path.with_span([&] (Span const &s) {
						if (s.equals(path))
							handle.handler.io_handle_watch(); }); });
			}

			void detach_channels(Span const &path) override
			{
				_root._detach_channels(path);
			}

			Root_parent_fs(Root &root) : _root(root) { }

		} _root_parent_fs { *this };

		Union_file_system _fs { *this, _root_parent_fs, File_system::Ident { "root" } };

		void _for_each_watch_handle(auto const &cond_fn, auto const &fn)
		{
			/* loop for conditionally visiting each item outside the registry mutex */
			for (;;) {
				Watch_handle *ptr = nullptr;
				_watch_handles.for_each([&] (Watch_handle &handle) {
					if (!ptr && cond_fn(handle)) ptr = &handle; });

				if (!ptr)
					break;
				fn(*ptr);
			}
		};

		void _detach_channels(Span const &path)
		{
			_for_each_watch_handle(
				[&] (Watch_handle &h) { return h.watching() && h.affected(path); },
				[&] (Watch_handle &h) { h.unwatch(); });

			_dir_handles.for_each([&] (Dir_handle &handle) {
				if (handle.affected(path))
					handle.detach(); });

			_file_handles.for_each([&] (File_handle &handle) {
				if (handle.affected(path))
					while (handle.detach() == File_handle::Detach_result::RETRY)
						commit_and_wait(); });
		}

		void _reattach_channels(Span const &path)
		{
			_for_each_watch_handle(
				[&] (Watch_handle &h) { return !h.watching() && h.affected(path); },
				[&] (Watch_handle &h) {
					if (h.watch().failed())
						warning("unable to re-watch ", h.path); });

			_file_handles.for_each([&] (File_handle &handle) {
				if (handle.affected(path))
					while (handle.attach() == File_handle::Attach_error::RETRY)
						commit_and_wait(); });
		}

	public:

		Root(Genode::Env &env, Allocator &alloc)
		:
			_env(env), _alloc(alloc), _user(*this)
		{ }

		Root(Genode::Env &env, Allocator &alloc, Env::User &user)
		:
			_env(env), _alloc(alloc), _user(user)
		{ }

		Root(Genode::Env &env, Allocator &alloc, Node const &config)
		:
			Root(env, alloc)
		{
			apply_config(config);
		}

		~Root() { apply_config(Node()); }

		Progress apply_config(Node const &config)
		{
			_detach_channels(Span::from_cstring("/"));

			Progress const result = _fs.update(config, _fs_factory);
			if (result.progressed)
				_fs.resume_after_update();

			_reattach_channels(Span::from_cstring("/"));
			return result;
		}

		Genode::Env      &env()              override { return _env; }
		Allocator        &alloc()            override { return _alloc; }
		File_system      &fs()               override { return _fs; }
		File_handles     &file_handles()     override { return _file_handles; }
		Dir_handles      &dir_handles()      override { return _dir_handles; }
		Watch_handles    &watch_handles()    override { return _watch_handles; }
		Deferred_wakeups &deferred_wakeups() override { return _deferred_wakeups; }
		Env::Io          &io()               override { return *this; }
		Env::User        &user()             override { return _user; }

		/**
		 * Env::Io interface
		 */
		void commit() override { _deferred_wakeups.trigger(); }

		/**
		 * Env::Io interface
		 */
		void commit_and_wait() override
		{
			_deferred_wakeups.trigger();
			_env.ep().wait_and_dispatch_one_io_signal();
		}

		/**
		 * Env::User interface
		 *
		 * Fallback implementation used if no 'user' is specified at
		 * construction time.
		 */
		void wakeup_vfs_user() override { };
};

#endif /* _INCLUDE__VFS__ROOT_H_ */
