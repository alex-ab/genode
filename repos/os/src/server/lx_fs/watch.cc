/*
 * \brief  File-system node
 * \author Pirmin Duss
 * \date   2020-06-17
 */

/*
 * Copyright (C) 2013-2020 Genode Labs GmbH
 * Copyright (C) 2020 gapfruit AG
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */


/* local includes */
#include "watch.h"
#include "notifier.h"


static bool lx_path_exists(Lx_fs::Watch_node::Path const &path)
{
	struct stat s { };
	return lstat(path.string(), &s) == 0;
}


static bool lx_dir_exists(Lx_fs::Watch_node::Path const &path)
{
	struct stat s { };
	return lstat(path.string(), &s) == 0 && S_ISDIR(s.st_mode);
}


static Lx_fs::Watch_node::Path without_trailing_slash(Lx_fs::Watch_node::Path path)
{
	using namespace Genode;
	return path.with_span([&] (Span const &s) {
		size_t n = s.num_bytes;
		while (n && s.start[n - 1] == '/') n--;
		return Lx_fs::Watch_node::Path(Cstring(s.start, n));
	});
}


Lx_fs::Watch_node::Watch_node(Env              &env,
                              Path       const &path,
                              Response_handler &response_handler,
                              Notifier         &notifier)
:
	Node { ~0u /* watch node is not tied to an inode */ },
	_env { env },
	_path { without_trailing_slash(path) },
	_response_handler { response_handler },
	_notifier { notifier }
{
	name(_path.string());

	_try_subscribe();
}

Lx_fs::Watch_node::~Watch_node()
{
	if (_watched_path.length() > 1)
		_notifier.remove_watch(_watched_path.string(), *this);
}


void Lx_fs::Watch_node::_try_subscribe()
{
	bool const path_exists = lx_path_exists(_path);

	if (path_exists && _subscribed())
		return;

	/* path appeared */
	if (path_exists && !_subscribed()) {
		_notifier.remove_watch(_watched_path.string(), *this);
		_watched_path = _path;
		_notifier.add_watch(_watched_path.string(), *this);
		_notify_handler.local_submit(); /* handle initial content */
		return;
	}

	/* path disappeared */
	if (!path_exists && _subscribed()) {
		_notifier.remove_watch(_watched_path.string(), *this);
		_watched_path = { };
	}

	/*
	 * Content does not exist yet. Watch inner-most existing compound directory.
	 */

	if (_watched_path.length() > 1) {
		_notifier.remove_watch(_watched_path.string(), *this);
		_watched_path = { };
	}

	bool any_dir_watched = false;

	_path.with_span([&] (Span const &s) {
		char buf[s.num_bytes] { };
		Genode::memcpy(buf, s.start, s.num_bytes);

		Path missing_dir { };

		for (size_t n = s.num_bytes; n > 0; ) {

			/* remove last path element */
			while (n && buf[n - 1] != '/') n--;
			if (n == 0)
				break;

			Path const dir_path_without_slash { Cstring(buf, n - 1) };

			if (lx_dir_exists(dir_path_without_slash)) {
				_watched_path = dir_path_without_slash;
				_notifier.add_watch(_watched_path.string(), *this);
				any_dir_watched = true;
				break;
			}

			missing_dir = dir_path_without_slash;

			if (n) n--; /* skip slash */
		}

		/* cover race if dir was created while subscribing */
		if (missing_dir.length() > 1)
			if (lx_dir_exists(missing_dir))
				_notify_handler.local_submit();

		if (!any_dir_watched)
			warning("no dir to watch for handling path ", _path);
	});

	/* cover race if file was created while subscribing */
	if (lx_path_exists(_path) && !_subscribed())
		_notify_handler.local_submit();
}


void Lx_fs::Watch_node::_handle_notify()
{
	if (!_subscribed()) {
		/* notification refers to a compound directory */
		_try_subscribe();
		return;
	}

	mark_as_updated();
	_acked_packet = Packet_descriptor { Packet_descriptor { },
	                                    Node_handle { _open_node->id().value },
	                                    Packet_descriptor::CONTENT_CHANGED,
	                                    0, 0 };
	_acked_packet.succeeded(true);

	_response_handler.handle_watch_node_response(*this);
}
