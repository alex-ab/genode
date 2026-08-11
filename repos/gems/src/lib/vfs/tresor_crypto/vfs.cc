/*
 * \brief  Integration of the Tresor block encryption
 * \author Martin Stein
 * \author Josef Soentgen
 * \date   2020-11-10
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <vfs/dir_file_system.h>
#include <vfs/single_file_system.h>
#include <util/arg_string.h>

/* vfs tresor crypto includes */
#include <interface.h>


namespace Vfs_tresor_crypto {

	using namespace Genode;
	using namespace Genode::Vfs;

	class Encrypt_file_system;
	class Decrypt_file_system;

	class Key_file_system;
	class Keys_file_system;

	class  Management_file_system;
	struct Add_key_file_system;
	struct Remove_key_file_system;

	class  File_system;
}


class Vfs_tresor_crypto::Encrypt_file_system : public Vfs::Single_file_system
{
	private:

		Tresor_crypto::Interface &_crypto;
		uint32_t _key_id;

		struct Encrypt_handle : Single_vfs_handle
		{
			Tresor_crypto::Interface &_crypto;
			uint32_t _key_id;

			enum State { NONE, PENDING };
			State _state;

			Encrypt_handle(Directory_service &ds,
			               Allocator &alloc, Tresor_crypto::Interface &crypto,
			               uint32_t key_id)
			:
				Single_vfs_handle(ds, alloc, 0),
				_crypto(crypto), _key_id(key_id), _state(State::NONE)
			{ }

			Read_result read(At, Byte_range_ptr const &dst) override
			{
				if (_state != State::PENDING)
					return Read_error::DENIED;

				_crypto.execute();

				try {
					Tresor_crypto::Interface::Complete_request const cr =
						_crypto.encryption_request_complete(dst);
					if (!cr.valid)
						return Read_error::DENIED;

					_state = State::NONE;
					return dst.num_bytes;

				} catch (Tresor_crypto::Interface::Buffer_too_small) { }

				return Read_error::DENIED;
			}

			Write_result write(At const at, Const_byte_range_ptr const &src) override
			{
				if (_state != State::NONE)
					return Write_error::DENIED;

				try {
					uint64_t const block_number = at.pos / Tresor_crypto::BLOCK_SIZE;
					bool const ok =
						_crypto.submit_encryption_request(block_number, _key_id, src);
					if (!ok)
						return Write_error::RETRY;
					_state = State::PENDING;
				} catch (Tresor_crypto::Interface::Buffer_too_small) {
					return Write_error::DENIED;
				}

				_crypto.execute();
				return src.num_bytes;
			}

			Ftruncate_result ftruncate(file_size) override { return FTRUNCATE_OK; }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }
		};

	public:

		Encrypt_file_system(Parent_fs &parent_fs, Tresor_crypto::Interface &crypto, uint32_t key_id)
		:
			Single_file_system(parent_fs, Node_type::TRANSACTIONAL_FILE, type_name(), Node_rwx::rw(), Node()),
			_crypto(crypto), _key_id(key_id)
		{ }

		static char const *type_name() { return "encrypt"; }

		char const *type() override { return type_name(); }

		Open_result open(char const *path, unsigned, Vfs_handle **out_handle,
		                 Allocator &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				*out_handle =
					new (alloc) Encrypt_handle(*this, alloc, _crypto, _key_id);
				return OPEN_OK;
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}
};


class Vfs_tresor_crypto::Decrypt_file_system : public Single_file_system
{
	private:

		Tresor_crypto::Interface   &_crypto;
		uint32_t  _key_id;

		struct Decrypt_handle : Single_vfs_handle
		{
			Tresor_crypto::Interface   &_crypto;
			uint32_t  _key_id;

			enum State { NONE, PENDING };
			State _state;

			Decrypt_handle(Directory_service        &ds,
			               Allocator                &alloc,
			               Tresor_crypto::Interface &crypto,
			               uint32_t                  key_id)
			:
				Single_vfs_handle(ds, alloc, 0), _crypto(crypto), _key_id(key_id), _state(State::NONE)
			{ }

			Read_result read(At, Byte_range_ptr const &dst) override
			{
				if (_state != State::PENDING)
					return Read_error::DENIED;

				_crypto.execute();

				try {
					Tresor_crypto::Interface::Complete_request const cr =
						_crypto.decryption_request_complete(dst);
					(void)cr;
					_state = State::NONE;
					return dst.num_bytes;
				} catch (Tresor_crypto::Interface::Buffer_too_small) { }

				return Read_error::DENIED;
			}

			Write_result write(At const at, Const_byte_range_ptr const &src) override
			{
				if (_state != State::NONE)
					return Write_error::DENIED;

				try {
					uint64_t const block_number = at.pos / Tresor_crypto::BLOCK_SIZE;
					bool const ok =
						_crypto.submit_decryption_request(block_number, _key_id, src);
					if (!ok)
						return Write_error::RETRY;
					_state = State::PENDING;
				} catch (Tresor_crypto::Interface::Buffer_too_small) {
					return Write_error::DENIED;
				}

				_crypto.execute();
				return src.num_bytes;
			}

			Ftruncate_result ftruncate(file_size) override { return FTRUNCATE_OK; }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }
		};

	public:

		Decrypt_file_system(Parent_fs &parent_fs, Tresor_crypto::Interface &crypto, uint32_t key_id)
		:
			Single_file_system(parent_fs, Node_type::TRANSACTIONAL_FILE, type_name(), Node_rwx::rw(), Node()),
			_crypto(crypto), _key_id(key_id)
		{ }

		static char const *type_name() { return "decrypt"; }

		char const *type() override { return type_name(); }

		Open_result open(char const *path, unsigned /* flags */,
		                 Vfs_handle **out_handle,
		                 Allocator &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				*out_handle =
					new (alloc) Decrypt_handle(*this, alloc, _crypto, _key_id);
				return OPEN_OK;
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}
};


class Vfs_tresor_crypto::Key_file_system : public Dir_file_system,
                                           private File_system_factory
{
	private:

		uint32_t _key_id;

		Encrypt_file_system _encrypt_fs;
		Decrypt_file_system _decrypt_fs;

		Vfs::File_system *create(Vfs::Env &, Parent_fs &, Node const &node) override
		{
			if (node.has_type(Encrypt_file_system::type_name()))
				return &_encrypt_fs;

			if (node.has_type(Decrypt_file_system::type_name()))
				return &_decrypt_fs;

			return nullptr;
		}

		using Config = String<128>;

		static Config _config(uint32_t key_id)
		{
			char buf[Config::capacity()] { };

			Generator::generate({ buf, sizeof(buf) }, "dir",
				[&] (Generator &g) {

					g.attribute("name", String<16>(key_id));

					g.node("decrypt");
					g.node("encrypt");

			}).with_error([] (Buffer_error) {
				warning("VFS-tresor_crypto key compound exceeds maximum buffer size"); });

			return Config(Cstring(buf));
		}

	public:

		Key_file_system(Vfs::Env &vfs_env, Parent_fs &parent_fs,
		                Tresor_crypto::Interface &crypto,
		                uint32_t key_id)
		:
			Dir_file_system(vfs_env, parent_fs, Node(_config(key_id))),
			_key_id(key_id),
			_encrypt_fs(*this, crypto, key_id),
			_decrypt_fs(*this, crypto, key_id)
		{
			Dir_file_system::update(Node(_config(key_id)), *this);
		}

		static char const *type_name() { return "keys"; }

		char const *type() override { return type_name(); }

		uint32_t key_id() const
		{
			return _key_id;
		}
};


class Vfs_tresor_crypto::Keys_file_system : public Vfs::File_system, public Vfs::Parent_fs
{
	private:

		Vfs::Env  &_vfs_env;
		Parent_fs &_parent_fs;

		bool _root_dir(char const *path) { return strcmp(path, "/keys") == 0; }
		bool _top_dir(char const *path) { return strcmp(path, "/") == 0; }

		struct Key_registry
		{
			Parent_fs &_parent_fs;
			Allocator &_alloc;
			Tresor_crypto::Interface &_crypto;

			struct Invalid_index : Exception { };
			struct Invalid_path  : Exception { };

			uint32_t _number_of_keys { 0 };

			Registry<Registered<Key_file_system>> _key_fs { };

			Key_registry(Parent_fs &parent_fs, Allocator &alloc, Tresor_crypto::Interface &crypto)
			:
				_parent_fs(parent_fs), _alloc(alloc), _crypto(crypto)
			{ }

			void update(Vfs::Env &vfs_env)
			{
				_crypto.for_each_key([&] (uint32_t const id) {

					bool already_known = false;
					auto lookup = [&] (Key_file_system &fs) {
						already_known |= fs.key_id() == id;
					};
					_key_fs.for_each(lookup);

					if (!already_known) {
						new (_alloc) Registered<Key_file_system>(
							_key_fs, vfs_env, _parent_fs, _crypto, id);
						++_number_of_keys;
					}
				});

				auto find_stale_keys = [&] (Key_file_system const &fs) {
					bool active_key = false;
					_crypto.for_each_key([&] (uint32_t const id) {
						active_key |= id == fs.key_id();
					});

					if (!active_key) {
						destroy(&_alloc, &const_cast<Key_file_system&>(fs));
						--_number_of_keys;
					}
				};
				_key_fs.for_each(find_stale_keys);
			}

			uint32_t number_of_keys() const { return _number_of_keys; }

			Key_file_system const &by_index(uint32_t idx) const
			{
				uint32_t i = 0;
				Key_file_system const *fsp { nullptr };
				auto lookup = [&] (Key_file_system const &fs) {
					if (i == idx) {
						fsp = &fs;
					}
					++i;
				};
				_key_fs.for_each(lookup);
				if (fsp == nullptr) {
					throw Invalid_index();
				}
				return *fsp;
			}

			Key_file_system &_by_id(uint32_t id)
			{
				Key_file_system *fsp { nullptr };
				auto lookup = [&] (Key_file_system &fs) {
					if (fs.key_id() == id) {
						fsp = &fs;
					}
				};
				_key_fs.for_each(lookup);
				if (fsp == nullptr) {
					throw Invalid_path();
				}
				return *fsp;
			}

			Key_file_system &by_path(char const *path)
			{
				if (!path) {
					throw Invalid_path();
				}

				if (path[0] == '/') {
					path++;
				}

				uint32_t id { 0 };
				ascii_to(path, id);
				return _by_id(id);
			}
		};

	public:

		struct Dir_vfs_handle : Vfs_handle
		{
			Key_registry const &_key_reg;

			bool const _root_dir { false };

			Read_result _query_keys(size_t index, Dirent &out)
			{
				if (index >= _key_reg.number_of_keys()) {
					out.type = Dirent_type::END;
					return sizeof(Dirent);
				}

				try {
					Key_file_system const &fs = _key_reg.by_index((unsigned)index);
					String<32> name { fs.key_id() };

					out = {
						.type = Dirent_type::DIRECTORY,
						.rwx  = Node_rwx::rx(),
						.name = { name.string() },
					};
					return sizeof(Dirent);

				} catch (Key_registry::Invalid_index) { }

				return Read_error::DENIED;
			}

			Read_result _query_root(size_t index, Dirent &out)
			{
				if (index == 0) {
					out = {
						.type = Dirent_type::DIRECTORY,
						.rwx  = Node_rwx::rx(),
						.name = { "keys" }
					};
				} else {
					out.type = Dirent_type::END;
				}
				return sizeof(Dirent);
			}

			Dir_vfs_handle(Directory_service  &ds,
			               Allocator          &alloc,
			               Key_registry const &key_reg,
			               bool                root_dir)
			:
				Vfs_handle(ds, alloc, 0),
				_key_reg(key_reg), _root_dir(root_dir)
			{ }

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (dst.num_bytes < sizeof(Dirent))
					return Read_error::DENIED;

				size_t const index = size_t(at.pos / sizeof(Dirent));

				Dirent &out = *(Dirent*)dst.start;

				if (!_root_dir) {

					/* opended as "/<name>" */
					return _query_keys(index, out);

				} else {
					/* opened as "/" */
					return _query_root(index, out);
				}
			}

			Ftruncate_result ftruncate(file_size) override { return FTRUNCATE_OK; }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }
		};

		struct Dir_snap_vfs_handle : Vfs_handle
		{
			Vfs_handle &vfs_handle;

			Dir_snap_vfs_handle(Directory_service &ds,
			                    Allocator         &alloc,
			                    Vfs_handle        &vfs_handle)
			:
				Vfs_handle(ds, alloc, 0), vfs_handle(vfs_handle)
			{ }

			~Dir_snap_vfs_handle()
			{
				vfs_handle.close();
			}

			Read_result read(At, Byte_range_ptr const &) override
			{
				warning("Tresor_crypto::Dir_snap_vfs_handle::complete_read not implemented");
				return Read_error::DENIED;
			}

			Ftruncate_result ftruncate(file_size) override { return FTRUNCATE_OK; }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }
		};

		Key_registry _key_reg;

		char const *_sub_path(char const *path) const
		{
			/* skip heading slash in path if present */
			if (path[0] == '/') {
				path++;
			}

			size_t const name_len = strlen(type_name());
			if (strcmp(path, type_name(), name_len) != 0) {
				return nullptr;
			}

			path += name_len;

			/*
			 * The first characters of the first path element are equal to
			 * the current directory name. Let's check if the length of the
			 * first path element matches the name length.
			 */
			if (*path != 0 && *path != '/') {
				return 0;
			}

			return path;
		}

		/**
		 * Parent_fs role for the children of this file system
		 */
		void notify_watchers(Span const &rel_path) override
		{
			using Path = String<MAX_PATH_LEN>;
			Path { type_name(), "/", Cstring(rel_path.start, rel_path.num_bytes) }
				.with_span([&] (Span const &s) {
					_parent_fs.notify_watchers(s); });
		}

		Keys_file_system(Vfs::Env &vfs_env, Parent_fs &parent_fs, Tresor_crypto::Interface &crypto)
		:
			_vfs_env(vfs_env), _parent_fs(parent_fs), _key_reg(*this, vfs_env.alloc(), crypto)
		{ }

		static char const *type_name() { return "keys"; }

		char const *type() override { return type_name(); }


		/*********************************
		 ** Directory service interface **
		 *********************************/

		Dataspace_capability dataspace(char const *) override
		{
			return Dataspace_capability();
		}

		void release(char const *, Dataspace_capability) override { }

		Open_result open(char const  *path,
		                 unsigned     mode,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			_key_reg.update(_vfs_env);

			path = _sub_path(path);
			if (!path || path[0] != '/') {
				return OPEN_ERR_UNACCESSIBLE;
			}

			try {
				Key_file_system &fs = _key_reg.by_path(path);
				return fs.open(path, mode, out_handle, alloc);
			} catch (Key_registry::Invalid_path) { }

			return OPEN_ERR_UNACCESSIBLE;
		}

		Opendir_result opendir(char const  *path,
		                       bool         create,
		                       Vfs_handle **out_handle,
		                       Allocator   &alloc) override
		{
			if (create) {
				return OPENDIR_ERR_PERMISSION_DENIED;
			}

			_key_reg.update(_vfs_env);

			bool const top = _top_dir(path);
			if (_root_dir(path) || top) {

				*out_handle = new (alloc) Dir_vfs_handle(*this, alloc,
				                                         _key_reg, top);
				return OPENDIR_OK;
			} else {
				char const *sub_path = _sub_path(path);
				if (!sub_path) {
					return OPENDIR_ERR_LOOKUP_FAILED;
				}
				try {
					Key_file_system &fs = _key_reg.by_path(sub_path);
					Vfs_handle *handle = nullptr;
					Opendir_result const res = fs.opendir(sub_path, create, &handle, alloc);
					if (res != OPENDIR_OK) {
						return OPENDIR_ERR_LOOKUP_FAILED;
					}
					*out_handle = new (alloc) Dir_snap_vfs_handle(*this,
					                                              alloc, *handle);
					return OPENDIR_OK;
				} catch (Key_registry::Invalid_path) { }
			}
			return OPENDIR_ERR_LOOKUP_FAILED;
		}

		void close(Vfs_handle *handle) override
		{
			if (handle && (&handle->ds() == this))
				destroy(handle->alloc(), handle);
		}

		Stat_result stat(char const *path, Stat &out_stat) override
		{
			out_stat = Stat { };
			path = _sub_path(path);

			/* path does not match directory name */
			if (!path) {
				return STAT_ERR_NO_ENTRY;
			}

			/*
			 * If path equals directory name, return information about the
			 * current directory.
			 */
			if (strlen(path) == 0 || _top_dir(path)) {
				out_stat.type   = Node_type::DIRECTORY;
				out_stat.device = (addr_t)this;
				return STAT_OK;
			}

			if (!path || path[0] != '/') {
				return STAT_ERR_NO_ENTRY;
			}

			try {
				Key_file_system &fs = _key_reg.by_path(path);
				Stat_result const res = fs.stat(path, out_stat);
				return res;
			} catch (Key_registry::Invalid_path) { }

			return STAT_ERR_NO_ENTRY;
		}

		Unlink_result unlink(char const *) override
		{
			return UNLINK_ERR_NO_PERM;
		}

		Rename_result rename(char const *, char const *) override
		{
			return RENAME_ERR_NO_PERM;
		}

		unsigned num_dirent(char const *path) override
		{
			_key_reg.update(_vfs_env);

			if (_top_dir(path) || _root_dir(path))
				return _key_reg.number_of_keys();

			path = _sub_path(path);
			if (!path) {
				return 0;
			}
			try {
				Key_file_system &fs = _key_reg.by_path(path);
				return fs.num_dirent(path);
			} catch (Key_registry::Invalid_path) {
				return 0;
			}
		}

		bool directory(char const *path) override
		{
			if (_root_dir(path)) {
				return true;
			}

			path = _sub_path(path);
			if (!path) {
				return false;
			}
			try {
				Key_file_system &fs = _key_reg.by_path(path);
				return fs.directory(path);
			} catch (Key_registry::Invalid_path) { }

			return false;
		}

		bool dir_entry_exists(char const *path) override
		{
			path = _sub_path(path);
			if (!path)
				return false;

			if (strlen(path) == 0 || strcmp(path, "") == 0)
				return true;

			try {
				Key_file_system &fs = _key_reg.by_path(path);
				if (fs.dir_entry_exists(path))
					return true;

			} catch (Key_registry::Invalid_path) { }

			return false;
		}
};


class Vfs_tresor_crypto::Management_file_system : public Single_file_system
{
	public:

		enum Type { ADD_KEY, REMOVE_KEY };

		static char const *type_string(Type type)
		{
			switch (type) {
			case Type::ADD_KEY:    return "add";
			case Type::REMOVE_KEY: return "remove";
			}
			return nullptr;
		}

	private:

		Management_file_system(Management_file_system const &) = delete;
		Management_file_system &operator=(Management_file_system const&) = delete;

		Type    _type;
		Tresor_crypto::Interface &_crypto;

		struct Manage_handle : Single_vfs_handle
		{
			Type    _type;
			Tresor_crypto::Interface &_crypto;

			Manage_handle(Directory_service        &ds,
			              Allocator                &alloc,
			              Type                      type,
			              Tresor_crypto::Interface &crypto)
			:
				Single_vfs_handle(ds, alloc, 0), _type(type), _crypto(crypto)
			{ }

			Read_result read(At, Byte_range_ptr const &) override
			{
				return Read_error::DENIED;
			}

			Write_result write(At const at, Const_byte_range_ptr const &src) override
			{
				if (at.pos != 0)
					return Write_error::DENIED;

				if (src.start == nullptr || src.num_bytes < sizeof (uint32_t))
					return Write_error::DENIED;

				uint32_t id = *reinterpret_cast<uint32_t const*>(src.start);
				if (id == 0)
					return Write_error::DENIED;

				if (_type == Type::ADD_KEY) {

					if (src.num_bytes != sizeof (uint32_t) + 32 /* XXX Tresor::Key::value*/)
						return Write_error::DENIED;

					try {
						char const * value     = src.start     + sizeof (uint32_t);
						size_t const value_len = src.num_bytes - sizeof (uint32_t);
						if (_crypto.add_key(id, value, value_len))
							return src.num_bytes;
					} catch (...) { }

				} else if (_type == Type::REMOVE_KEY) {

					if (src.num_bytes != sizeof (uint32_t))
						return Write_error::DENIED;

					if (_crypto.remove_key(id))
						return src.num_bytes;
				}

				return Write_error::DENIED;
			}

			Ftruncate_result ftruncate(file_size) override { return FTRUNCATE_OK; }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }
		};

		char const *_type_name;

	public:

		Management_file_system(Parent_fs &parent_fs, Tresor_crypto::Interface &crypto,
		                       Type type, char const *type_name)
		:
			Single_file_system(parent_fs, Node_type::TRANSACTIONAL_FILE,
			                   type_name, Node_rwx::wo(), Node()),
			_type(type), _crypto(crypto), _type_name(type_name)
		{ }

		char const *type() override { return _type_name; }

		Open_result open(char const  *path,
		                 unsigned    /* flags */,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path)) {
				return OPEN_ERR_UNACCESSIBLE;
			}

			try {
				*out_handle =
					new (alloc) Manage_handle(*this, alloc, _type, _crypto);
				return OPEN_OK;
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			Stat_result result = Single_file_system::stat(path, out);
			return result;
		}
};


struct Vfs_tresor_crypto::Add_key_file_system : Vfs_tresor_crypto::Management_file_system
{
	static char const *type_name() { return "add_key"; }

	Add_key_file_system(Parent_fs &parent_fs, Tresor_crypto::Interface &crypto)
	:
		Management_file_system(parent_fs, crypto, Management_file_system::ADD_KEY, type_name()) { }

	char const *type() override { return type_name(); }
};


struct Vfs_tresor_crypto::Remove_key_file_system : Vfs_tresor_crypto::Management_file_system
{
	static char const *type_name() { return "remove_key"; }

	Remove_key_file_system(Parent_fs &parent_fs, Tresor_crypto::Interface &crypto)
	: Management_file_system(parent_fs, crypto, Management_file_system::REMOVE_KEY, type_name()) { }

	char const *type() override { return type_name(); }
};


struct Vfs_tresor_crypto::File_system : Dir_file_system, File_system_factory
{
	private:

		Tresor_crypto::Interface &_crypto;

		Keys_file_system       _keys_fs;
		Add_key_file_system    _add_key_fs;
		Remove_key_file_system _remove_key_fs;

		Vfs::File_system *create(Vfs::Env &, Parent_fs &, Node const &node) override
		{
			if (node.has_type(Add_key_file_system::type_name()))    return &_add_key_fs;
			if (node.has_type(Remove_key_file_system::type_name())) return &_remove_key_fs;
			if (node.has_type(Keys_file_system::type_name()))       return &_keys_fs;

			return nullptr;
		}

		using Config = String<128>;

		static Config _config(Node const &node)
		{
			(void)node;
			char buf[Config::capacity()] { };

			Generator::generate({ buf, sizeof(buf) }, "dir",
				[&] (Generator &g) {
					g.attribute(
						"name", node.attribute_value("name", String<64>("")));

					g.node("add_key",    [&] () { });
					g.node("remove_key", [&] () { });
					g.node("keys",       [&] () { });
			}).with_error([] (Buffer_error) {
				warning("VFS-tresor_crypto compound exceeds maximum buffer size"); });

			return Config(Cstring(buf));
		}

	public:

		File_system(Vfs::Env &vfs_env, Parent_fs &parent_fs, Node const &node)
		:
			Dir_file_system(vfs_env, parent_fs, Node(_config(node))),
			_crypto(Tresor_crypto::get_interface()),
			_keys_fs(vfs_env, *this, _crypto),
			_add_key_fs(*this, _crypto),
			_remove_key_fs(*this, _crypto)
		{
			Dir_file_system::update(Node(_config(node)), *this);
		}
};


/**************************
 ** VFS plugin interface **
 **************************/

extern "C" Genode::Vfs::File_system_factory *vfs_file_system_factory(void)
{
	using namespace Genode;
	using namespace Genode::Vfs;

	struct Factory : File_system_factory
	{
		File_system *create(Vfs::Env &vfs_env, Parent_fs &parent_fs, Node const &node) override
		{
			try {
				return new (vfs_env.alloc())
					Vfs_tresor_crypto::File_system(vfs_env, parent_fs, node);
			} catch (...) {
				error("could not create 'tresor_crypto' file system");
			}
			return nullptr;
		}
	};

	static Factory factory;
	return &factory;
}
