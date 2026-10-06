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

	struct File_channel;
	struct Dir_channel;

	class Node;
	class File;
	struct Symlink;
	class Directory;
	class File_system;
}


struct Vfs_ram::File_channel : Vfs::File_channel, private List<File_channel>::Element
{
	friend List<File_channel>;

	Allocator   &_alloc;
	File_system &_fs;

	Vfs_ram::Node &node;

	/* track if this channel has modified its node */
	bool modifying = false;

	using Path = String<MAX_PATH_LEN>;

	Path const path; /* needed for deferred unlink-on-close to look up the parent */

	File_channel(Allocator &alloc, Attr attr, File_system &fs,
	             Vfs_ram::Node &node, Path const &path)
	:
		Vfs::File_channel(attr), _alloc(alloc), _fs(fs), node(node), path(path)
	{ }

	inline Write_result write(At, Const_byte_range_ptr const &) override;
	inline Read_result  read(At, Byte_range_ptr const &) override;

	bool read_ready () const override { return true; }
	bool write_ready() const override { return true; }

	inline Resize_result resize(file_size) override;
	inline Sync_result sync() override;
	inline Update_mtime_result update_mtime(Timestamp) override;

	void destruct() override;
};


struct Vfs_ram::Dir_channel : Vfs::Dir_channel
{
	File_system &_fs;
	Allocator   &_alloc;
	Directory   &_dir;

	Dir_channel(File_system &fs, Allocator &alloc, Directory &dir)
	:
		_fs(fs), _alloc(alloc), _dir(dir)
	{ }

	void destruct() override { destroy(_alloc, this); }

	inline Read_dir_result read_dir(At, Dirent_range_ptr const &) override;
};


class Vfs_ram::Node : private Avl_node<Node>
{
	private:

		friend class Avl_node<Node>;
		friend class Avl_tree<Node>;
		friend class List<File_channel>;
		friend class List<File_channel>::Element;
		friend class Directory;

		char _name[MAX_NAME_LEN];

		List<File_channel> _file_channels { };

		bool _marked_as_unlinked = false;

	public:

		Timestamp mtime { };

		Node(char const *node_name) { name(node_name); }

		virtual ~Node() { }

		char const *name() { return _name; }
		void name(char const *name) { copy_cstring(_name, name, MAX_NAME_LEN); }

		virtual size_t length() = 0;

		void open(File_channel &c) { _file_channels.insert(&c); }

		bool opened() const
		{
			return _file_channels.first() != nullptr;
		}

		void close(File_channel &c) { _file_channels.remove(&c); }

		void mark_as_unlinked() { _marked_as_unlinked = true; }

		bool marked_as_unlinked() const { return _marked_as_unlinked; }

		Node_rwx rwx() const
		{
			return { .readable   = true,
			         .writeable  = true,
			         .executable = true };
		}

		using Read_result = File_channel::Read_result;
		using Read_error  = File_channel::Read_error;

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
				return File_channel::Read_eof();

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


struct Vfs_ram::Symlink : Vfs_ram::Node
{
	using Target = String<MAX_PATH_LEN>;

	Target target { };

	Symlink(char const *name) : Node(name) { }

	size_t length() override { return strlen(target.string()); }
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

		Dir_channel::Read_dir_result read_dir(Dir_channel::At at, Dirent_range_ptr const &dst)
		{
			if (dst.num_entries < 1)
				return Dir_channel::Read_dir_error::DENIED;

			Node *node_ptr = _entries.first();
			size_t index = at.pos;
			if (node_ptr) node_ptr = node_ptr->index(index);
			if (!node_ptr)
				return Dir_channel::Read_dir_end();

			Node &node = *node_ptr;

			auto dirent_type = [&] ()
			{
				if (dynamic_cast<Directory *>(node_ptr)) return Dirent_type::DIRECTORY;
				if (dynamic_cast<Symlink   *>(node_ptr)) return Dirent_type::SYMLINK;
				return Dirent_type::FILE;
			};

			dst.start[0] = {
				.type = dirent_type(),
				.rwx  = node.rwx(),
				.name = { node.name() }
			};
			return 1;
		}
};


class Vfs_ram::File_system : public Vfs::File_system
{
	private:

		friend class List<Vfs_ram::Watch_handle>;
		friend class File_channel;

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

		void _try_complete_unlink(File_channel::Path const &path,
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

		File_system(Vfs::Env &env, Parent_fs &parent_fs, Genode::Node const &node)
		:
			Vfs::File_system(Ident::from_node(node)), _env(env), _parent_fs(parent_fs)
		{ }

		~File_system() { _root.empty(_env.alloc()); }

		unsigned num_dirent(char const *path) override
		{
			if (Node * const node = lookup(path))
				if (Directory * const dir = dynamic_cast<Directory *>(node))
					return unsigned(dir->length());

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

		Open_result open(char const *path, Open_attr attr, Allocator &alloc) override
		{
			File *file;
			char const * const name = basename(path);

			if (attr.create) {
				Directory * const parent = lookup_parent(path);

				if (!parent)
					return Open_error::DENIED;

				if (parent->child(name))
					return Open_error::DENIED;

				if (strlen(name) >= MAX_NAME_LEN)
					return Open_error::DENIED;

				try { file = new (_env.alloc()) File(name, _env.alloc()); }
				catch (Out_of_memory) { return Open_error::DENIED; }
				parent->adopt(file);
				_notify_compound_dir_watchers(path);
			} else {
				Node * const node = lookup(path);
				if (!node) return Open_error::DENIED;

				file = dynamic_cast<File *>(node);
				if (!file) return Open_error::DENIED;
			}

			try {
				File_channel &channel = *new (alloc)
					File_channel(alloc, { .writeable = attr.writeable }, *this, *file, path);
				file->open(channel);
				return channel;
			} catch (Out_of_ram) {
				if (attr.create) {
					lookup_parent(path)->release(file);
					remove(file);
				}
				return Open_error::OUT_OF_RAM;
			} catch (Out_of_caps) {
				if (attr.create) {
					lookup_parent(path)->release(file);
					remove(file);
				}
				return Open_error::OUT_OF_CAPS;
			}
		}

		Opendir_result opendir(char const * const path, Allocator &alloc) override
		{
			Directory * const parent = lookup_parent(path);
			if (!parent)
				return Opendir_error::DENIED;

			Node * const node = lookup(path);
			if (!node) return Opendir_error::DENIED;

			Directory *dir = dynamic_cast<Directory *>(node);
			if (!dir) return Opendir_error::DENIED;

			try {
				return *new (alloc) Dir_channel(*this, alloc, *dir);
			}
			catch (Out_of_ram)  { return Opendir_error::OUT_OF_RAM; }
			catch (Out_of_caps) { return Opendir_error::OUT_OF_CAPS; }

			return Opendir_error::DENIED;
		}

		Stat_result stat(char const *path, Stat &stat) override
		{
			Node * const node_ptr = lookup(path);
			if (!node_ptr)
				return Stat_result::DENIED;

			Node &node = *node_ptr;

			auto dirent_type = [&] ()
			{
				if (dynamic_cast<Directory *>(node_ptr)) return Dirent_type::DIRECTORY;
				if (dynamic_cast<Symlink   *>(node_ptr)) return Dirent_type::SYMLINK;

				return Dirent_type::FILE;
			};

			stat = {
				.size              = node.length(),
				.type              = dirent_type(),
				.rwx               = node.rwx(),
				.modification_time = node.mtime
			};

			return Stat_result::OK;
		}

		Rename_result rename(char const * const from, char const * const to) override
		{
			if ((strcmp(from, to) == 0) && lookup(from))
				return Rename_result::OK;

			char const * const new_name = basename(to);
			if (strlen(new_name) >= MAX_NAME_LEN)
				return Rename_result::DENIED;

			Directory * const from_dir = lookup_parent(from);
			if (!from_dir)
				return Rename_result::DENIED;

			Directory * const to_dir = lookup_parent(to);
			if (!to_dir)
				return Rename_result::DENIED;

			Node * const from_node = from_dir->child(basename(from));
			if (!from_node)
				return Rename_result::DENIED;

			Node * const to_node = to_dir->child(new_name);
			if (to_node) {

				if (Directory * const dir = dynamic_cast<Directory*>(to_node))
					if (dir->length() || (!dynamic_cast<Directory*>(from_node)))
						return Rename_result::DENIED;

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

			return Rename_result::OK;
		}

		Unlink_result unlink(char const * const path) override
		{
			Directory * const parent = lookup_parent(path);
			if (!parent)
				return Unlink_result::DENIED;

			Node * const node = parent->child(basename(path));
			if (!node)
				return Unlink_result::DENIED;

			/* defer unlink of a node that is still referenced by a file channel */
			node->mark_as_unlinked();

			_try_complete_unlink({ Cstring(path) }, parent, *node);

			return Unlink_result::OK;
		}

		Mkdir_result mkdir(char const *path, Timestamp ts) override
		{
			Directory * const parent = lookup_parent(path);
			if (!parent)
				return Mkdir_result::DENIED;

			char const * const name = basename(path);
			if (strlen(name) >= MAX_NAME_LEN)
				return Mkdir_result::DENIED;

			if (*name == '\0')
				return Mkdir_result::UPDATED; /* already exists */

			if (Node * node = lookup(path)) {
				/* update timestamp of existing directory */
				if (Directory *dir = dynamic_cast<Directory*>(node)) {
					dir->mtime = ts;
					return Mkdir_result::UPDATED;
				}
				return Mkdir_result::DENIED; /* conflict with file or symlink */
			}

			try {
				Directory &dir = *new (_env.alloc()) Directory(name);
				parent->adopt(&dir);
				_notify_compound_dir_watchers(path);
				dir.mtime = ts;
			}
			catch (Out_of_caps) { return Mkdir_result::DENIED; }
			catch (Out_of_ram)  { return Mkdir_result::DENIED; }

			_notify_watchers(path);
			_notify_compound_dir_watchers(path);
			return Mkdir_result::CREATED;
		}

		Symlink_result symlink(char const *path, char const *target, Timestamp ts) override
		{
			Directory * const parent = lookup_parent(path);
			if (!parent)
				return Symlink_result::DENIED;

			char const * const name = basename(path);
			if (strlen(name) >= MAX_NAME_LEN)
				return Symlink_result::DENIED;

			if (strlen(target) > MAX_PATH_LEN)
				return Symlink_result::DENIED;

			if (*name == '\0')
				return Symlink_result::DENIED;

			if (Node * node = lookup(path)) {
				/* update target and timestamp of existing symlink */
				if (Symlink *symlink = dynamic_cast<Symlink*>(node)) {
					symlink->target = target;
					symlink->mtime = ts;
					return Symlink_result::UPDATED;
				}
				return Symlink_result::DENIED; /* conflict with file or dir */
			}

			Symlink &symlink = *new (_env.alloc()) Symlink(name);
			parent->adopt(&symlink);
			symlink.target = target;
			symlink.mtime = ts;

			_notify_watchers(path);
			_notify_compound_dir_watchers(path);
			return Symlink_result::CREATED;
		}

		Follow_result follow(char const *path, Byte_range_ptr const &dst) override
		{
			if (!dst.num_bytes) /* no space for null-termination */
				return Follow_error::DENIED;

			return follow_path(Span::from_cstring(path),
				[&] (auto const &partial_path) {
					return dynamic_cast<Symlink *>(lookup(partial_path.string()));
				},
				[&] (Path_elem const elem, auto const &partial_path) {
					Symlink &symlink = *dynamic_cast<Symlink *>(lookup(partial_path.string()));
					symlink.target.with_span([&] (Span const &src) {
						size_t n = min(dst.num_bytes,
						               src.num_bytes + 1 /* null termination */);
						copy_cstring(dst.start, src.start, n);
					});
					return elem;
				});
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

		static constexpr auto BUILTIN_FS_TYPE = "ram";
};


Vfs_ram::File_channel::Write_result Vfs_ram::File_channel::write(At at, Const_byte_range_ptr const &buf)
{
	if (!writeable)
		return Write_error::DENIED;

	modifying = true;
	return node.write(buf, Seek { size_t(at.pos) });
}


Vfs_ram::File_channel::Read_result Vfs_ram::File_channel::read(At at, Byte_range_ptr const &dst)
{
	return node.read(dst,  Seek { size_t(at.pos) });
}


Vfs_ram::File_channel::Resize_result Vfs_ram::File_channel::resize(file_size len)
{
	if (!writeable)
		return Resize_result::DENIED;

	Seek const at { size_t(len) };

	try { node.truncate(at); }
	catch (Out_of_memory) { return Resize_result::DENIED; }
	return Resize_result::OK;
}


Genode::Vfs::Dir_channel::Read_dir_result Vfs_ram::Dir_channel::read_dir(At at, Dirent_range_ptr const &dst)
{
	return _dir.read_dir(at, dst);
}


Vfs_ram::Sync_result Vfs_ram::File_channel::sync()
{
	if (modifying) {
		modifying = false;
		node.close(*this);
		_fs._notify_watchers(path.string());
		node.open(*this);
	}
	return Sync_result::OK;
}


Genode::Vfs::File_channel::Update_mtime_result Vfs_ram::File_channel::update_mtime(Timestamp ts)
{
	if (writeable) {
		modifying = true;
		node.mtime = ts;
	}
	return Update_mtime_result::OK;
}


void Vfs_ram::File_channel::destruct()
{
	/* copy out members needed after destroy */
	bool   const node_modified = this->modifying;
	Path   const path          = this->path;
	Node        &node          = this->node;
	File_system &fs            = this->_fs;

	Directory * const parent_ptr = _fs.lookup_parent(path.string());

	node.close(*this);
	destroy(_alloc, this);

	if (node_modified)
		path.with_span([&] (Span const &s) {
			fs._parent_fs.notify_watchers(s); });

	fs._try_complete_unlink(path, parent_ptr, node);
}


#endif /* _INCLUDE__VFS__RAM_FILE_SYSTEM_H_ */
