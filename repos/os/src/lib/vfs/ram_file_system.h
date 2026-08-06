/*
 * \brief  Embedded RAM VFS
 * \author Emery Hemingway
 * \date   2015-07-21
 */

/*
 * Copyright (C) 2015-2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__RAM_FILE_SYSTEM_H_
#define _INCLUDE__VFS__RAM_FILE_SYSTEM_H_

#include <ram_fs/chunk.h>
#include <ram_fs/param.h>
#include <vfs/file_system.h>
#include <dataspace/client.h>
#include <util/avl_tree.h>

namespace Vfs_ram {

	using namespace Genode;
	using namespace Genode::Vfs;
	using namespace Ram_fs;

	using ::File_system::Chunk;
	using ::File_system::Chunk_index;

	enum { MAX_NAME_LEN = 128 };

	using Out_of_memory = Allocator::Out_of_memory;

	/**
	 * Return base-name portion of null-terminated path string
	 */
	static inline char const *basename(char const *path)
	{
		char const *start = path;

		for (; *path; ++path)
			if (*path == '/')
				start = path + 1;

		return start;
	}

	using Seek = ::File_system::Chunk_base::Seek;

	struct Io_handle;

	class Node;
	class File;
	class Symlink;
	class Directory;
	class File_system;
}


struct Vfs_ram::Io_handle final : Vfs_handle, private List<Io_handle>::Element
{
	friend List<Io_handle>;

	File_system &_fs;

	Vfs_ram::Node &node;

	/* Track if this handle has modified its node */
	bool modifying = false;

	using Path = String<MAX_PATH_LEN>;

	Path const path; /* needed for deferred unlink-on-close to look up the parent */

	Io_handle(Directory_service &ds,
	          File_system       &fs,
	          Allocator         &alloc,
	          int                status_flags,
	          Vfs_ram::Node     &node,
	          Path        const &path)
	:
		Vfs_handle(ds, alloc, status_flags), _fs(fs), node(node), path(path)
	{ }

	inline Write_result write(Const_byte_range_ptr const &) override;
	inline Read_result  read(Byte_range_ptr const &) override;

	bool read_ready () const override { return true; }
	bool write_ready() const override { return true; }

	inline Ftruncate_result ftruncate(file_size) override;
	inline Sync_result sync() override;
	inline bool update_modification_timestamp(Timestamp) override;
};


class Vfs_ram::Node : private Avl_node<Node>
{
	private:

		friend class Avl_node<Node>;
		friend class Avl_tree<Node>;
		friend class List<Io_handle>;
		friend class List<Io_handle>::Element;
		friend class Directory;

		char _name[MAX_NAME_LEN];

		List<Io_handle> _io_handles { };

		Timestamp _modification_time { };

		bool _marked_as_unlinked = false;

	public:

		Node(char const *node_name) { name(node_name); }

		virtual ~Node() { }

		char const *name() { return _name; }
		void name(char const *name) { copy_cstring(_name, name, MAX_NAME_LEN); }

		virtual size_t length() = 0;

		void open(Io_handle &handle) { _io_handles.insert(&handle); }

		bool opened() const
		{
			return _io_handles.first() != nullptr;
		}

		void close(Io_handle &handle) { _io_handles.remove(&handle); }

		void mark_as_unlinked() { _marked_as_unlinked = true; }

		bool marked_as_unlinked() const { return _marked_as_unlinked; }

		bool update_modification_timestamp(Timestamp time)
		{
			_modification_time = time;
			return true;
		}

		Timestamp modification_time() const { return _modification_time; }

		Node_rwx rwx() const
		{
			return { .readable   = true,
			         .writeable  = true,
			         .executable = true };
		}

		virtual Read_result read(Byte_range_ptr const &, Seek)
		{
			error("Vfs_ram::Node::read() called");
			return Read_error::DENIED;
		}

		virtual size_t write(Const_byte_range_ptr const &, Seek)
		{
			error("Vfs_ram::Node::write() called");
			return 0;
		}

		virtual void truncate(Seek)
		{
			error("Vfs_ram::Node::truncate() called");
		}


		/************************
		 ** Avl node interface **
		 ************************/

		bool higher(Node *c) { return (strcmp(c->_name, _name) > 0); }

		/**
		 * Find index N by walking down the tree N times,
		 * not the most efficient way to do this.
		 */
		Node *index(size_t &i)
		{
			if (!_marked_as_unlinked) {
				if (i-- == 0)
					return this;
			}

			Node *n;

			n = child(LEFT);
			if (n)
				n = n->index(i);

			if (n) return n;

			n = child(RIGHT);
			if (n)
				n = n->index(i);

			return n;
		}

		Node *sibling(const char * const name)
		{
			if (strcmp(name, _name) == 0) return this;

			Node * const c =
				Avl_node<Node>::child(strcmp(name, _name) > 0);
			return c ? c->sibling(name) : nullptr;
		}
};


class Vfs_ram::File : public Vfs_ram::Node
{
	private:

		using Chunk_level_3 = Chunk      <num_level_3_entries()>;
		using Chunk_level_2 = Chunk_index<num_level_2_entries(), Chunk_level_3>;
		using Chunk_level_1 = Chunk_index<num_level_1_entries(), Chunk_level_2>;
		using Chunk_level_0 = Chunk_index<num_level_0_entries(), Chunk_level_1>;

		Chunk_level_0 _chunk;

		size_t _length = 0;

	public:

		File(char const * const name, Allocator &alloc)
		: Node(name), _chunk(alloc, Seek{0}) { }

		Read_result read(Byte_range_ptr const &dst, Seek seek) override
		{
			size_t const chunk_used_size = _chunk.used_size();

			if (seek.value >= _length)
				return 0;

			/*
			 * Constrain read transaction to available chunk data
			 *
			 * Note that 'chunk_used_size' may be lower than '_length'
			 * because 'Chunk' may have truncated tailing zeros.
			 */

			size_t const len = (seek.value + dst.num_bytes >= _length)
			                 ? _length - min(_length, seek.value)
			                 : dst.num_bytes;

			size_t read_len = len;

			if (seek.value + read_len > chunk_used_size) {
				if (chunk_used_size >= seek.value)
					read_len = chunk_used_size - seek.value;
				else
					read_len = 0;
			}

			_chunk.read(Byte_range_ptr(dst.start, read_len), seek);

			/* add zero padding if needed */
			if (read_len < dst.num_bytes)
				bzero(dst.start + read_len, len - read_len);

			return len;
		}

		size_t write(Const_byte_range_ptr const &src, Seek const seek) override
		{
			size_t const at = (seek.value == ~0UL) ? _chunk.used_size() : seek.value;

			size_t len = src.num_bytes;

			if (at + src.num_bytes >= Chunk_level_0::SIZE)
				len = Chunk_level_0::SIZE - at + src.num_bytes;

			try { _chunk.write(src, Seek{at}); }
			catch (Out_of_memory) { return 0; }

			/*
			 * Keep track of file length. We cannot use 'chunk.used_size()'
			 * as file length because trailing zeros may by represented
			 * by zero chunks, which do not contribute to 'used_size()'.
			 */
			_length = max(_length, at + len);

			return len;
		}

		size_t length() override { return _length; }

		void truncate(Seek size) override
		{
			if (size.value < _chunk.used_size())
				_chunk.truncate(size);

			_length = size.value;
		}
};


class Vfs_ram::Symlink : public Vfs_ram::Node
{
	private:

		char   _target[MAX_PATH_LEN];
		size_t _len = 0;

	public:

		Symlink(char const *name) : Node(name) { }

		size_t length() override { return _len; }

		Read_result read(Byte_range_ptr const &dst, Seek) override
		{
			size_t n = min(dst.num_bytes, _len);
			memcpy(dst.start, _target, n);
			return n;
		}

		size_t write(Const_byte_range_ptr const &src, Seek) override
		{
			if (src.num_bytes > MAX_PATH_LEN)
				return 0;

			size_t len = src.num_bytes;

			for (size_t i = 0; i < len; ++i) {
				if (src.start[i] == '\0') {
					len = i + 1; /* number of characters + terminating zero */
					break;
				}
			}

			_len = len;
			memcpy(_target, src.start, _len);

			return len;
		}
};


class Vfs_ram::Directory : public Vfs_ram::Node
{
	private:

		Avl_tree<Node> _entries { };

		size_t _count = 0;

	public:

		Directory(char const *name) : Node(name) { }

		void empty(Allocator &alloc)
		{
			while (Node *node = _entries.first()) {
				_entries.remove(node);
				if (File *file = dynamic_cast<File*>(node)) {
					if (file->opened())
						continue;
				} else if (Directory *dir = dynamic_cast<Directory*>(node)) {
					dir->empty(alloc);
				}
				destroy(alloc, node);
			}
		}

		void adopt(Node *node)
		{
			_entries.insert(node);
			++_count;
		}

		Node *child(char const *name)
		{
			Node * const node = _entries.first();
			return node ? node->sibling(name) : nullptr;
		}

		void release(Node *node)
		{
			_entries.remove(node);
			--_count;
		}

		size_t length() override { return _count; }

		Read_result read(Byte_range_ptr const &dst, Seek const seek) override
		{
			using Dirent = Directory_service::Dirent;

			if (dst.num_bytes < sizeof(Dirent))
				return Read_error::DENIED;

			size_t index = seek.value / sizeof(Dirent);

			Dirent &dirent = *(Dirent*)dst.start;

			using Dirent_type = Directory_service::Dirent_type;

			Node *node_ptr = _entries.first();
			if (node_ptr) node_ptr = node_ptr->index(index);
			if (!node_ptr) {
				dirent.type = Dirent_type::END;
				return sizeof(Dirent);
			}

			Node &node = *node_ptr;

			auto dirent_type = [&] ()
			{
				if (dynamic_cast<File      *>(node_ptr)) return Dirent_type::CONTINUOUS_FILE;
				if (dynamic_cast<Directory *>(node_ptr)) return Dirent_type::DIRECTORY;
				if (dynamic_cast<Symlink   *>(node_ptr)) return Dirent_type::SYMLINK;

				return Dirent_type::END;
			};

			Dirent_type const type = dirent_type();

			if (type == Dirent_type::END)
				return 0;

			dirent = {
				.type = type,
				.rwx  = node.rwx(),
				.name = { node.name() }
			};

			return sizeof(Dirent);
		}
};


class Vfs_ram::File_system : public Vfs::File_system
{
	private:

		friend class List<Vfs_ram::Watch_handle>;
		friend class Io_handle;

		Vfs::Env &_env;

		Parent_fs &_parent_fs;

		Directory  _root = { "" };

		Node *lookup(char const *path, bool return_parent = false)
		{
			if (*path ==  '/') ++path;
			if (*path == '\0') return &_root;

			char buf[MAX_PATH_LEN];
			copy_cstring(buf, path, MAX_PATH_LEN);
			Directory *dir = &_root;

			char *name = &buf[0];
			for (size_t i = 0; i < MAX_PATH_LEN; ++i) {
				if (buf[i] == '/') {
					buf[i] = '\0';

					Node * const node = dir->child(name);
					if (!node) return nullptr;

					dir = dynamic_cast<Directory *>(node);
					if (!dir) return nullptr;

					/* set the current name aside */
					name = &buf[i+1];
				} else if (buf[i] == '\0') {
					if (return_parent)
						return dir;
					else
						return dir->child(name);
				}
			}
			return nullptr;
		}

		Directory *lookup_parent(char const *path)
		{
			Node * const node = lookup(path, true);
			if (node)
				return dynamic_cast<Directory *>(node);
			return nullptr;
		}

		void remove(Node *node)
		{
			if (File * const file = dynamic_cast<File*>(node)) {
				if (file->opened()) {
					file->mark_as_unlinked();
					return;
				}
			} else if (Directory *dir = dynamic_cast<Directory*>(node)) {
				dir->empty(_env.alloc());
			}

			destroy(_env.alloc(), node);
		}

		void _try_complete_unlink(Io_handle::Path const &path,
		                          Directory *parent_ptr, Node &node)
		{
			if (node.marked_as_unlinked() && !node.opened()) {
				if (parent_ptr)
					parent_ptr->release(&node);
				remove(&node);

				/* notify watchers for the unlinked node and its compound dir */
				path.with_span([&] (Span const &s) {
					_parent_fs.notify_watchers(s);
					with_compound_dir(s, [&] (Span const &dir_path) {
						_parent_fs.notify_watchers(dir_path); });
				});
			}
		}

		void _notify_watchers(char const *path)
		{
			_parent_fs.notify_watchers(Span::from_cstring(path));
		}

		void _notify_compound_dir_watchers(char const *path)
		{
			with_compound_dir(Span::from_cstring(path), [&] (Span const &dir_path) {
				_parent_fs.notify_watchers(dir_path); });
		}

	public:

		File_system(Vfs::Env &env, Parent_fs &parent_fs, Genode::Node const &)
		:
			_env(env), _parent_fs(parent_fs)
		{ }

		~File_system() { _root.empty(_env.alloc()); }


		/*********************************
		 ** Directory service interface **
		 *********************************/

		file_size num_dirent(char const *path) override
		{
			if (Node * const node = lookup(path))
				if (Directory * const dir = dynamic_cast<Directory *>(node))
					return dir->length();

			return 0;
		}

		bool directory(char const * const path) override
		{
			Node * const node = lookup(path);
			return node
				? (dynamic_cast<Directory *>(node) != nullptr)
				: false;
		}

		bool dir_entry_exists(char const *path) override {
			return lookup(path) != nullptr; }

		Open_result open(char const * const path, unsigned mode,
		                 Vfs_handle **handle, Allocator &alloc) override
		{
			File *file;
			char const * const name = basename(path);
			bool const       create = mode & OPEN_MODE_CREATE;

			if (create) {
				Directory * const parent = lookup_parent(path);

				if (!parent)
					return OPEN_ERR_UNACCESSIBLE;

				if (parent->child(name))
					return OPEN_ERR_EXISTS;

				if (strlen(name) >= MAX_NAME_LEN)
					return OPEN_ERR_NAME_TOO_LONG;

				try { file = new (_env.alloc()) File(name, _env.alloc()); }
				catch (Out_of_memory) { return OPEN_ERR_NO_SPACE; }
				parent->adopt(file);
				_notify_compound_dir_watchers(path);
			} else {
				Node * const node = lookup(path);
				if (!node) return OPEN_ERR_UNACCESSIBLE;

				file = dynamic_cast<File *>(node);
				if (!file) return OPEN_ERR_UNACCESSIBLE;
			}

			try {
				Io_handle * const io_handle_ptr = new (alloc)
					Io_handle(*this, *this, alloc, mode, *file, path);
				file->open(*io_handle_ptr);
				*handle = io_handle_ptr;
				return OPEN_OK;
			} catch (Out_of_ram) {
				if (create) {
					lookup_parent(path)->release(file);
					remove(file);
				}
				return OPEN_ERR_OUT_OF_RAM;
			} catch (Out_of_caps) {
				if (create) {
					lookup_parent(path)->release(file);
					remove(file);
				}
				return OPEN_ERR_OUT_OF_CAPS;
			}
		}

		Opendir_result opendir(char const * const path, bool create,
		                       Vfs_handle **handle, Allocator &alloc) override
		{
			Directory * const parent = lookup_parent(path);
			if (!parent)
				return OPENDIR_ERR_LOOKUP_FAILED;

			char const * const name = basename(path);

			Directory *dir;

			if (create) {
				if (*name == '\0')
					return OPENDIR_ERR_NODE_ALREADY_EXISTS;

				if (strlen(name) >= MAX_NAME_LEN)
					return OPENDIR_ERR_NAME_TOO_LONG;

				if (parent->child(name))
					return OPENDIR_ERR_NODE_ALREADY_EXISTS;

				try { dir = new (_env.alloc()) Directory(name); }
				catch (Out_of_memory) { return OPENDIR_ERR_NO_SPACE; }

				parent->adopt(dir);
				_notify_compound_dir_watchers(path);
			} else {

				Node * const node = lookup(path);
				if (!node) return OPENDIR_ERR_LOOKUP_FAILED;

				dir = dynamic_cast<Directory *>(node);
				if (!dir) return OPENDIR_ERR_LOOKUP_FAILED;
			}

			try {
				Io_handle * const io_handle_ptr = new (alloc)
					Io_handle(*this, *this, alloc, Io_handle::STATUS_RDONLY, *dir, path);
				dir->open(*io_handle_ptr);
				*handle = io_handle_ptr;
				return OPENDIR_OK;
			} catch (Out_of_ram) {
				if (create) {
					parent->release(dir);
					remove(dir);
				}
				return OPENDIR_ERR_OUT_OF_RAM;
			} catch (Out_of_caps) {
				if (create) {
					parent->release(dir);
					remove(dir);
				}
				return OPENDIR_ERR_OUT_OF_CAPS;
			}
		}

		Openlink_result openlink(char const * const path, bool create,
		                         Vfs_handle **handle, Allocator &alloc) override
		{
			Directory * const parent = lookup_parent(path);
			if (!parent)
				return OPENLINK_ERR_LOOKUP_FAILED;

			char const * const name = basename(path);

			Symlink *link;

			Node * const node = parent->child(name);

			if (create) {

				if (node)
					return OPENLINK_ERR_NODE_ALREADY_EXISTS;

				if (strlen(name) >= MAX_NAME_LEN)
					return OPENLINK_ERR_NAME_TOO_LONG;

				try { link = new (_env.alloc()) Symlink(name); }
				catch (Out_of_memory) { return OPENLINK_ERR_NO_SPACE; }

				parent->adopt(link);
				_notify_compound_dir_watchers(path);
			} else {

				if (!node)
					return OPENLINK_ERR_LOOKUP_FAILED;

				link = dynamic_cast<Symlink *>(node);
				if (!link) return OPENLINK_ERR_LOOKUP_FAILED;
			}

			try {
				Io_handle * const io_handle_ptr = new (alloc)
					Io_handle(*this, *this, alloc, Io_handle::STATUS_RDWR, *link, path);
				link->open(*io_handle_ptr);
				*handle = io_handle_ptr;
				return OPENLINK_OK;
			} catch (Out_of_ram) {
				if (create) {
					parent->release(link);
					remove(link);
				}
				return OPENLINK_ERR_OUT_OF_RAM;
			} catch (Out_of_caps) {
				if (create) {
					parent->release(link);
					remove(link);
				}
				return OPENLINK_ERR_OUT_OF_CAPS;
			}
		}

		void close(Vfs_handle *vfs_handle) override
		{
			Io_handle * const ram_handle =
				static_cast<Io_handle *>(vfs_handle);

			Node &node = ram_handle->node;
			bool const   node_modified = ram_handle->modifying;
			Io_handle::Path const path = ram_handle->path;

			Directory * const parent_ptr = lookup_parent(path.string());

			node.close(*ram_handle);
			destroy(vfs_handle->alloc(), ram_handle);

			if (node_modified)
				path.with_span([&] (Span const &s) {
					_parent_fs.notify_watchers(s); });

			_try_complete_unlink(path, parent_ptr, node);
		}

		Stat_result stat(char const *path, Stat &stat) override
		{
			Node * const node_ptr = lookup(path);
			if (!node_ptr)
				return STAT_ERR_NO_ENTRY;

			Node &node = *node_ptr;

			auto node_type = [&] ()
			{
				if (dynamic_cast<Directory *>(node_ptr)) return Node_type::DIRECTORY;
				if (dynamic_cast<Symlink   *>(node_ptr)) return Node_type::SYMLINK;

				return Node_type::CONTINUOUS_FILE;
			};

			stat = {
				.size              = node.length(),
				.type              = node_type(),
				.rwx               = node.rwx(),
				.device            = (addr_t)this,
				.modification_time = node.modification_time()
			};

			return STAT_OK;
		}

		Rename_result rename(char const * const from, char const * const to) override
		{
			if ((strcmp(from, to) == 0) && lookup(from))
				return RENAME_OK;

			char const * const new_name = basename(to);
			if (strlen(new_name) >= MAX_NAME_LEN)
				return RENAME_ERR_NO_PERM;

			Directory * const from_dir = lookup_parent(from);
			if (!from_dir)
				return RENAME_ERR_NO_ENTRY;

			Directory * const to_dir = lookup_parent(to);
			if (!to_dir)
				return RENAME_ERR_NO_ENTRY;

			Node * const from_node = from_dir->child(basename(from));
			if (!from_node)
				return RENAME_ERR_NO_ENTRY;

			Node * const to_node = to_dir->child(new_name);
			if (to_node) {

				if (Directory * const dir = dynamic_cast<Directory*>(to_node))
					if (dir->length() || (!dynamic_cast<Directory*>(from_node)))
						return RENAME_ERR_NO_PERM;

				/* detach node to be replaced from directory */
				to_dir->release(to_node);

				/* free the node that is replaced */
				remove(to_node);
			}

			from_dir->release(from_node);
			from_node->name(new_name);
			to_dir->adopt(from_node);

			_notify_watchers(from);
			_notify_watchers(to);
			_notify_compound_dir_watchers(from);
			_notify_compound_dir_watchers(to);

			return RENAME_OK;
		}

		Unlink_result unlink(char const * const path) override
		{
			Directory * const parent = lookup_parent(path);
			if (!parent)
				return UNLINK_ERR_NO_ENTRY;

			Node * const node = parent->child(basename(path));
			if (!node)
				return UNLINK_ERR_NO_ENTRY;

			/* defer unlink of a node that is still referenced by an Io_handle */
			node->mark_as_unlinked();

			_try_complete_unlink({ Cstring(path) }, parent, *node);

			return UNLINK_OK;
		}

		Dataspace_capability dataspace(char const * const path) override
		{
			Node * const node = lookup(path);
			if (!node)
				return { };

			File * const file = dynamic_cast<File *>(node);
			if (!file)
				return { };

			size_t const len = file->length();

			return _env.env().ram().try_alloc(len).convert<Dataspace_capability>(
				[&] (Ram::Allocation &allocation) {
					return _env.env().rm().attach(allocation.cap, {
						.size = { },  .offset     = { },  .use_at    = { },
						.at   = { },  .executable = { },  .writeable = true
					}).convert<Dataspace_capability>(
						[&] (Genode::Env::Local_rm::Attachment &a) {
							(void)file->read(Byte_range_ptr((char *)a.ptr, len), Seek{0});
							allocation.deallocate = false;
							return allocation.cap;
						},
						[&] (Genode::Env::Local_rm::Error) {
							return Dataspace_capability();
						}
					);
				},
				[&] (Ram_allocator::Alloc_error) { return Dataspace_capability(); }
			);
		}

		void release(char const *, Dataspace_capability ds_cap) override
		{
			_env.env().ram().free(
				static_cap_cast<Ram_dataspace>(ds_cap));
		}

		/***************************
		 ** File_system interface **
		 ***************************/

		static char const *name()   { return "ram"; }
		char const *type() override { return "ram"; }
};


Vfs_ram::Write_result Vfs_ram::Io_handle::write(Const_byte_range_ptr const &buf)
{
	if (!writeable())
		return Write_error::DENIED;

	Seek const seek { size_t(Vfs_handle::seek()) };

	size_t const num_bytes = node.write(buf, seek);
	modifying = true;

	return num_bytes;
}


Vfs_ram::Read_result Vfs_ram::Io_handle::read(Byte_range_ptr const &dst)
{
	Seek const seek { size_t(Vfs_handle::seek()) };

	return node.read(dst, seek);
}


Vfs_ram::Ftruncate_result Vfs_ram::Io_handle::ftruncate(file_size len)
{
	if (!writeable())
		return FTRUNCATE_ERR_NO_PERM;

	Seek const at { size_t(len) };

	try { node.truncate(at); }
	catch (Out_of_memory) { return FTRUNCATE_ERR_NO_SPACE; }
	return FTRUNCATE_OK;
}


Vfs_ram::Sync_result Vfs_ram::Io_handle::sync()
{
	if (modifying) {
		modifying = false;
		node.close(*this);
		_fs._notify_watchers(path.string());
		node.open(*this);
	}
	return Sync_result::OK;
}


bool Vfs_ram::Io_handle::update_modification_timestamp(Timestamp time)
{
	if (!writeable())
		return false;

	modifying = true;

	return node.update_modification_timestamp(time);
}

#endif /* _INCLUDE__VFS__RAM_FILE_SYSTEM_H_ */
