/*
 * \brief  procfs emulation
 * \author Johannes Schlatow
 * \date   2026-09-09
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */


/* libc-internal includes */
#include <internal/config.h>
#include <internal/procfs.h>

using namespace Libc;

Procfs::Path Procfs::_pid_path_from_config(Config const &config)
{
	Path path { config.proc };
	if (path != "")
		path = Directory::join(path, Genode::String<16>(config.pid).string());

	return path;
}


Procfs::Path Procfs::_self_path_from_config(Config const &config)
{
	Path path { config.proc };
	if (path != "")
		path = Directory::join(path, "self");

	return path;
}


Procfs::Path Procfs::_fd_path(int id) const
{
	if (!_fd_present)
		return Path { };

	Path path = _pid_path;
	path = Directory::join(Directory::join(path, "fd"), Genode::String<16>(id));

	return path;
}


void Procfs::add_fd_file_from_kernel(int id, Path const &target)
{
	if (_fd_present)
		_fs._vfs.symlink(_fd_path(id).string(), target.string(), Vfs::Timestamp { });
}


void Procfs::add_fd_file(int id, Path const &target)
{
	if (_fd_present)
		_fs.symlink(target.string(), _fd_path(id).string());
}


void Procfs::remove_fd_file(int id)
{
	if (_fd_present)
		_fs.unlink(_fd_path(id).string());
}


Absolute_path Procfs::resolve_proc_self(Absolute_path path) const
{
	/* replace /proc/self by /proc/<PID> */
	if (_fd_present && path.strip_prefix(_self_path.string())) {
		Absolute_path new_path { _pid_path };
		new_path.append_element(path.string());
		return new_path;
	}

	return path;
}


Procfs::Procfs(Fs &fs, Config const &config)
: _fs(fs), _config(config)
{
	auto mkdir = [&] (Absolute_path const &path) -> bool {
		if (!_fs._root_dir.directory_exists(path.string())) {
			if (_fs._vfs.mkdir(path.string(), Vfs::Timestamp { }) != Vfs::Mkdir_result::CREATED)
				Genode::error("Unable to create directory ", path);
			else
				return true;
		} else {
			return true;
		}

		return false;
	};

	/* create /proc/<PID>/fd */
	Absolute_path path = _pid_path;
	path.remove_trailing('/');
	if (_pid_path != "" && mkdir(path)) {
		path.append_element("fd");
		_fd_present = mkdir(path);
	}
}
