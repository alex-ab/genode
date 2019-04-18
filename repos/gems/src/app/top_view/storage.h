/*
 * \brief  Persistent helper
 * \author Alexander Boettcher
 * \date   2019-04-18
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <file_system_session/connection.h>

namespace Top {
	class Storage;
	using namespace Genode;
	using namespace File_system;
}

class Top::Storage
{
	private:

		Env &env;
		Heap heap { env.pd(), env.rm() };
		Allocator_avl avl_alloc { &heap };
		File_system::Connection fs { env, avl_alloc, "store", "/", true };
		File_system::Session::Tx::Source &pkt_tx { *fs.tx() };
		Dir_handle dir_handle { fs.dir("/", false) };
		File_handle file_handle { fs.file(dir_handle, "trace.top", READ_WRITE, true /* create */) };

	public:

		Storage(Env &env) : env(env) { }
};
