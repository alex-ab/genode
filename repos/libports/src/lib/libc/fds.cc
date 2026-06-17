/*
 * \brief  File descriptor definition and lifetime management
 * \author Norman Feske
 * \date   2026-06-23
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <os/path.h>

/* libc-internal includes */
#include <internal/fds.h>

using namespace Libc;


Libc::File_descriptor::Path Libc::File_descriptor::ioctl_dir() const
{
	if (path.length() <= 1) {
		warning("Libc::vfs_ioctl_dir: fd lacks path information");
		return { };
	}

	Genode::Path<Vfs::MAX_PATH_LEN> abs_path { path.string() };

	/*
	 * The pseudo files used for ioctl operations reside in a (hidden)
	 * directory named after the device path and prefixed with '.'.
	 */
	String<64> const ioctl_dir_name(".", abs_path.last_element());

	abs_path.strip_last_element();
	abs_path.append_element(ioctl_dir_name.string());

	return { abs_path.string() };
}


/********************
 ** Libc functions **
 ********************/

extern "C" int __attribute__((weak)) getdtablesize(void) { return MAX_NUM_FDS; }
