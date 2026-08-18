/*
 * \brief  Jitterentropy based random file system
 * \author Josef Soentgen
 * \date   2014-08-19
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <vfs/env.h>

/* local includes */
#include <vfs_jitterentropy.h>


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		Vfs::File_system *create(Vfs::Env &env, Vfs::Parent_fs &parent_fs, Node const &node) override
		{
			return new (env.alloc())
				Vfs_jitterentropy::File_system(parent_fs, env.alloc(), node);
		}
	};

	static Factory factory;
	return &factory;
}
