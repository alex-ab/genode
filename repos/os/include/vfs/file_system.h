/*
 * \brief  VFS file-system back-end interface
 * \author Norman Feske
 * \date   2011-02-17
 */

/*
 * Copyright (C) 2011-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__FILE_SYSTEM_H_
#define _INCLUDE__VFS__FILE_SYSTEM_H_

#include <vfs/types.h>

namespace Genode::Vfs { struct File_system; }


struct Genode::Vfs::File_system : Interface, Noncopyable
{
	struct Factory : Interface
	{
		struct Attr { Vfs::File_system &fs; };

		using Instance = Genode::Allocation<Vfs::File_system::Factory>;

		enum class Error { DENIED };

		/**
		 * Create and return a new file-system instance
		 */
		virtual Instance::Attempt create(Vfs::Env &, Parent_fs &, Node const &) = 0;

		virtual void _free(Instance &) = 0;
	};

	/**
	 * File-system identity used for updating the union fs via 'List_model'
	 */
	struct Ident
	{
		String<100> string;

		/**
		 * Return file-system ident string from node type and attribute
		 *
		 * This function composes identity from the note type and
		 * attributes. Sub nodes are not part of the identity.
		 */
		inline static Ident from_node(Node const &node);

	} const _ident;

	/**
	 * Construct file system with the specified identity
	 */
	File_system(Ident const &ident) : _ident(ident) { }

	/**
	 * Adjust to configuration changes
	 *
	 * Note that it is not possible to access files of the VFS during the
	 * update. If a file system depends on files provided by anoher file
	 * system, 'resume_after_update' can be used to interact with those
	 * files when the VFS has reached a new consistent state.
	 */
	virtual Progress update(Node const &, Factory &) { return STALLED; }

	/**
	 * Hook for plugins to reconnect to files after an update
	 */
	virtual void resume_after_update() { }

	/**
	 * Hook for implementing 'Factory::_free' for VFS plugins
	 */
	virtual void destruct() { };

	/**
	 * Return true if the node corresponds to the file system's identity
	 *
	 * By default, the identity comprises the node's type, name, and all
	 * attributes. Whenever any of those aspects change, the file system is
	 * replaced by a new instance. In contrast, a file system that is able
	 * to respond to updated attributes while keeping its identity intact
	 * would implement 'matches' by excluding the parameter attributes.
	 */
	virtual bool matches(Node const &node) const
	{
		return Ident::from_node(node).string == _ident.string;
	}

	virtual Dataspace_capability dataspace(char const *path)
	{
		(void)path; return { };
	}

	virtual void release(char const *path, Dataspace_capability) { (void)path; }

	struct Open_attr { bool writeable, create; };

	virtual Open_result open(char const *path, Open_attr, Allocator &)
	{
		(void)path; return Open_error::DENIED;
	}

	virtual Opendir_result opendir(char const *path, Allocator &)
	{
		(void)path; return Opendir_error::DENIED;
	}

	/**
	 * Subscribe to watch notifications for the given relative path
	 *
	 * The file system is expected to call 'Parent_dir::notify_watchers'
	 * whenver the given file or directory is modified.
	 *
	 * If the file or directory exists at 'watch' time, 'notify_watchers'
	 * is expected to be called immediately.
	 */
	virtual Watch_result watch(char const *path) { (void)path; return Ok(); }

	/**
	 * Unsubscribe from watch notifications for the given relative path
	 */
	virtual void unwatch(char const *path) { (void)path; }

	struct Stat
	{
		file_size   size;
		Dirent_type type;
		Node_rwx    rwx;
		Timestamp   modification_time;
	};

	virtual Stat_result stat(char const *path, Stat &)
	{
		(void)path; return Stat_result::DENIED;
	}

	/**
	 * Create directory
	 *
	 * If the directory already exists, its modification time is updated to the
	 * provided timestamp.
	 */
	virtual Mkdir_result mkdir(char const *path, Timestamp)
	{
		(void)path; return Mkdir_result::DENIED;
	}

	/**
	 * Return number of directory entries located at given path
	 */
	virtual unsigned num_dirent(char const *path) { (void)path; return 0; }

	virtual bool directory(char const *path) { (void)path; return false; }

	/**
	 * Return leaf path or nullptr if the path does not exist
	 */
	virtual bool dir_entry_exists(char const *path) { (void)path; return false; }

	/**
	 * Create symbolic link
	 *
	 * If the symlink already exists, its link target and modification time is
	 * updated.
	 */
	virtual Symlink_result symlink(char const *path, char const *target, Timestamp)
	{
		(void)path; (void)target; return Symlink_result::DENIED;
	}

	/**
	 * De-reference symlink
	 *
	 * \return index of de-referenced path element, starting at index 0
	 *
	 * If the returned value equals the number of path elements, the path
	 * does not contain a symlink.
	 *
	 * If a symlink was deferenced, the 'dst' buffer contains the symlink's
	 * target, including a null termination.
	 */
	virtual Follow_result follow(char const *path, Byte_range_ptr const &dst) = 0;

	virtual Unlink_result unlink(char const *path)
	{
		(void)path; return Unlink_result::DENIED;
	}

	virtual Rename_result rename(char const *from, char const *to)
	{
		(void)from; (void)to; return Rename_result::DENIED;
	}
};


Genode::Vfs::File_system::Ident
Genode::Vfs::File_system::Ident::from_node(Node const &node)
{
	char buf[decltype(string)::capacity()] { };

	return Generator::generate(Byte_range_ptr(buf, sizeof(buf)),
	                           node.type(), [&] (Generator &g) {
		g.node_attributes(node);
	}).convert<Ident>(
		[&] (size_t len) {
			len = max(len, 3u) - 3u;  /* omit HID end marker and line breaks */
			return Ident { { Cstring(buf, len) } };
		},
		[&] (Buffer_error) {
			warning("dropping attributes for VFS identity of: ", node);
			return Ident { node.type() };
	});
}

#endif /* _INCLUDE__VFS__FILE_SYSTEM_H_ */
