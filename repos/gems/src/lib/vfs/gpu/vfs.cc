/*
 * \brief  Minimal file system for GPU session
 * \author Sebastian Sumpf
 * \date   2021-10-14
 *
 * The file system only handles completion signals of the GPU session in order
 * to work from non-EP threads (i.e., pthreads) in libc components. A read
 * returns only in case a completion signal has been delivered since the
 * previous call to read.
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <gpu_session/connection.h>
#include <os/vfs.h>
#include <vfs/single_file_system.h>

#include "vfs_gpu.h"

namespace Vfs_gpu
{
	using namespace Genode;
	using namespace Genode::Vfs;

	struct File_system;
}

struct Vfs_gpu::File_system : Single_file_system
{
	struct Gpu_vfs_handle : Single_vfs_handle
	{
		/* allow for initial read to query the ID */
		bool             _complete { true };
		Vfs::Env        &_env;
		Gpu::Connection  _gpu_session { _env.env() };

		Io_signal_handler<Gpu_vfs_handle> _completion_sigh {
			_env.env().ep(), *this, &Gpu_vfs_handle::_handle_completion };

		using Id_space = Genode::Id_space<Gpu_vfs_handle>;

		Id_space::Element const _elem;

		void _handle_completion()
		{
			_complete = true;
			_env.user().wakeup_vfs_user();
		}

		Gpu_vfs_handle(Vfs::Env &env,
		               Directory_service &ds,
		               Allocator &alloc,
		               Id_space &space)
		:
			Single_vfs_handle(ds, alloc, 0),
			_env(env), _elem(*this, space)
		{
			_gpu_session.completion_sigh(_completion_sigh);
		}

		Read_result read(At, Byte_range_ptr const &dst) override
		{
			if (!_complete) return Read_error::RETRY;

			unsigned long const id_value = _elem.id().value;

			if (dst.num_bytes < sizeof(id_value))
				return Read_error::DENIED;

			_complete = false;
			memcpy(dst.start, &id_value, sizeof(id_value));

			return sizeof(id_value);
		}

		bool read_ready()  const override { return _complete; }
		bool write_ready() const override { return true; }

		Id_space::Id id() const { return _elem.id(); }
	};

	Vfs::Env &_env;

	using Config = String<32>;

	Id_space<Gpu_vfs_handle> _handle_space { };

	File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &config)
	:
		Single_file_system(parent_fs, {
			.ident = Ident::from_node(config),
			.name  = File::Name::from_node(config),
			.rwx   = File::RO
		}),
		_env(env)
	{ }

	void destruct() override { destroy(_env.alloc(), this); }

	Open_result open(char const  *path, unsigned,
	                 Vfs::Vfs_handle **out_handle,
	                 Allocator   &alloc) override
	{
		if (!_single_file(path))
			return OPEN_ERR_UNACCESSIBLE;

		try {
			Gpu_vfs_handle *handle  = new (alloc)
				Gpu_vfs_handle(_env, *this, alloc, _handle_space);

			*out_handle = handle;

			return OPEN_OK;
		}
		catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
		catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
	}

	static char const *type_name() { return "gpu"; }
};


static Vfs_gpu::File_system *_fs { nullptr };

/**
 * XXX: return GPU session for given ID, returned on every 'read()' call
 * This function is used, for example, by libdrm
 */
Gpu::Connection *vfs_gpu_connection(unsigned long id)
{
	if (!_fs) return nullptr;

	using Gpu_vfs_handle = Vfs_gpu::File_system::Gpu_vfs_handle;
	using Id_space       = Genode::Id_space<Gpu_vfs_handle>;

	try {
		return _fs->_handle_space.apply<Gpu_vfs_handle>(
			Id_space::Id { .value = id },
			[] (Gpu_vfs_handle &handle)
			{
				return &handle._gpu_session;
			}
		);
	} catch (...) { }

	return nullptr;
}


static Genode::Vfs::Env *_env { nullptr };

Genode::Env *vfs_gpu_env()
{
	return _env ? &_env->env() : nullptr;
}


/**************************
 ** VFS plugin interface **
 **************************/

extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		using Fs = Vfs_gpu::File_system;

		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &parent_fs,
		                         Node const &node) override
		{
			_env = &env;
			try {
				return { *this, { *new (env.alloc()) Fs(env, parent_fs, node) } };
			}
			catch (...) { error("could not create 'gpu_fs' "); }
			return Error::DENIED;
		}

		void _free(Instance &instance) override { instance.fs.destruct(); };
	};

	static Factory factory;
	return &factory;
}
