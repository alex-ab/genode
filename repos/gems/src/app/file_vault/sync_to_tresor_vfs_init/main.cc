/*
 * \brief  Synchronize the File Vault to the Tresor VFS initialization
 * \author Martin Stein
 * \date   2021-03-19
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <os/vfs.h>

using namespace Genode;

void Component::construct(Env &env)
{
	Heap heap { env.ram(), env.rm() };

	Root_directory root { env, heap };

	Attached_rom_dataspace config { env, "config" };

	config.node().with_sub_node("vfs",
		[&] (Node const &config) { root.apply_config(config); },
		[&]                      { error("VFS not configured"); });

	{ Append_file { root, Directory::Path("/tresor/tresor/current/data"), { } }; }

	env.parent().exit(0);
}
