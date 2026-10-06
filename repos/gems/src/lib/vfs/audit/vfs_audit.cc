/*
 * \brief  VFS audit plugin
 * \author Emery Hemingway
 * \date   2018-03-12
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <vfs/env.h>
#include <log_session/connection.h>

namespace Vfs_audit {

	using namespace Genode;
	using namespace Genode::Vfs;

	class File_system;
}

class Vfs_audit::File_system : public Vfs::File_system
{
	private:

		class Log : public Output
		{
			private:

				enum { BUF_SIZE = Log_session::MAX_STRING_LEN };

				Log_connection _log;

				char _buf[BUF_SIZE];
				unsigned _num_chars = 0;

				void _flush()
				{
					_buf[_num_chars] = '\0';
					_log.write(Log_session::String(_buf, _num_chars+1));
					_num_chars = 0;
				}

			public:

				Log(Genode::Env &env, char const *label)
				: _log(env, label) { }

				void out_char(char c) override
				{
					_buf[_num_chars++] = c;
					if (_num_chars >= sizeof(_buf)-1)
						_flush();
				}

				template <typename... ARGS>
				void log(ARGS &&... args)
				{
					Output::out_args(*this, args...);
					_flush();
				}

		} _audit_log;

		void _log(auto &&... args) { _audit_log.log(args...); }

		Allocator &_alloc;

		Vfs::File_system &_fs;

		Absolute_path const _audit_path;

		Absolute_path _expanded_path { }; /* buffer for 'dir_entry_exists' return value */

		/**
		 * Expand a path to lay within the audit path
		 */
		Absolute_path _expand(char const *path)
		{
			return Absolute_path(path+1, _audit_path.string());
		}

		struct File_channel : Vfs::File_channel
		{
			Allocator &_alloc;
			Log       &_audit_log;

			Absolute_path const path;

			Vfs::File_channel &audited;

			void _log(auto &&... args) { _audit_log.log(args...); }

			File_channel(Allocator &alloc, Attr attr, char const *path,
			             Log &log, Vfs::File_channel &audited)
			:
				Vfs::File_channel(attr),
				_alloc(alloc), _audit_log(log), path(path), audited(audited)
			{ }

			~File_channel()
			{
				_log("close ", path.string());
				audited.destruct();
			}

			Write_result write(At const at, Const_byte_range_ptr const &src) override
			{
				Write_result const result = audited.write(at, src);

				result.with_result(
					[&] (size_t n) {
						_log("wrote to ", path, " ", n, " / ", src.num_bytes);
					},
					[&] (Write_error e) {
						if (e == Write_error::RETRY)
							_log("write stalled for ", path);
						else
							_log("write failed for ", path);
					});

				return result;
			}

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				Read_result const result = audited.read(at, dst);

				result.with_result(
					[&] (size_t num_bytes) {
						_log("completed read from ", path, " ", num_bytes); },
					[&] (Read_error e) {
						if (result == Read_error::RETRY)
							_log("read needs retry for ", path);
						else
							_log("read error ", (int)e, " for ", path);
					});

				return result;
			}

			bool read_ready() const override
			{
				return audited.read_ready();
			}

			bool write_ready() const override
			{
				return audited.write_ready();
			}

			void notify_read_ready() override
			{
				audited.notify_read_ready();
			}

			Resize_result resize(file_size len) override
			{
				_log(__func__, " ", path, " ", len);
				return audited.resize(len);
			}

			Sync_result sync() override
			{
				Sync_result const result = audited.sync();

				if (result == Sync_result::RETRY) _log("syncing ", path);
				if (result == Sync_result::OK)    _log("synced ", path);

				return result;
			}

			void destruct() override { destroy(_alloc, this); }
		};

		struct Dir_channel : Vfs::Dir_channel
		{
			Allocator &_alloc;
			Log       &_audit_log;

			Absolute_path const path;

			Vfs::Dir_channel &audited;

			void _log(auto &&... args) { _audit_log.log(args...); }

			Dir_channel(Allocator &alloc, char const *path, Log &log,
			            Vfs::Dir_channel &audited)
			:
				_alloc(alloc), _audit_log(log), path(path), audited(audited)
			{ }

			void destruct() override
			{
				audited.destruct();
				destroy(_alloc, this);
			}

			Read_dir_result read_dir(At const at, Dirent_range_ptr const &dst) override
			{
				return audited.read_dir(at, dst).convert<Read_dir_result>(
					[&] (unsigned num_entries) {
						_log("completed read dir from ", path, " ", num_entries);
						return num_entries;
					},
					[&] (Read_dir_error e) {
						if (e == Read_dir_error::RETRY)
							_log("read dir needs retry for ", path);
						else
							_log("read dir error ", (int)e, " for ", path);
						return e;
					});
			}
		};

	public:

		File_system(Vfs::Env &env, Node const &config)
		:
			Vfs::File_system(Ident::from_node(config)),
			_audit_log(env.env(), config.attribute_value("label", String<64>("audit")).string()),
			_alloc(env.alloc()), _fs(env.fs()),
			_audit_path(config.attribute_value(
				"path", String<Absolute_path::capacity()>()))
		{ }

		void destruct() override { destroy(_alloc, this); }

		Dataspace_capability dataspace(const char *path) override
		{
			_log(__func__, " ", path);
			return _fs.dataspace(_expand(path).string());
		}

		void release(char const *path, Dataspace_capability ds) override
		{
			_log(__func__, " ", path);
			return _fs.release(_expand(path).string(), ds);
		}

		Open_result open(const char *path, Open_attr attr, Allocator &alloc) override
		{
			_log(__func__, " ", path, " writeable=", attr.writeable);

			return _fs.open(_expand(path).string(), attr, alloc).convert<Open_result>(
				[&] (Vfs::File_channel &audited) -> Open_result {
					Open_error error = Open_error::DENIED;
					try {
						Vfs::File_channel &c = *new (alloc)
							File_channel(alloc, { .writeable = attr.writeable },
							             path, _audit_log, audited);
						return c;
					}
					catch (Out_of_ram)  { error = Open_error::OUT_OF_RAM;  }
					catch (Out_of_caps) { error = Open_error::OUT_OF_CAPS; }
					audited.destruct();
					return error;
				},
				[&] (Open_error e) -> Open_result { return e; }
			);
		}

		Opendir_result opendir(char const *path, Allocator &alloc) override
		{
			_log(__func__, " ", path);

			return _fs.opendir(_expand(path).string(), alloc).convert<Opendir_result>(
				[&] (Vfs::Dir_channel &audited) -> Opendir_result {
					try {
						return *new (alloc) Dir_channel(alloc, path, _audit_log, audited);
					}
					catch (Out_of_ram)  { return Opendir_error::OUT_OF_RAM;  }
					catch (Out_of_caps) { return Opendir_error::OUT_OF_CAPS; }
				},
				[&] (Opendir_error e) -> Opendir_result { return e; });
		}

		Stat_result stat(const char *path, Vfs::File_system::Stat &buf) override
		{
			_log(__func__, " ", path);
			return _fs.stat(_expand(path).string(), buf);
		}

		Unlink_result unlink(const char *path) override
		{
			_log(__func__, " ", path);
			return _fs.unlink(_expand(path).string());
		}

		Rename_result rename(const char *from , const char *to) override
		{
			_log(__func__, " ", from, " ", to);
			return _fs.rename(_expand(from).string(), _expand(to).string());
		}

		Mkdir_result mkdir(char const *path, Timestamp ts) override
		{
			_log(__func__, " ", path);
			return _fs.mkdir(_expand(path).string(), ts);
		}

		Symlink_result symlink(char const *path, char const *target, Timestamp ts) override
		{
			_log(__func__, " ", path, " target: ", target);
			return _fs.symlink(_expand(path).string(), target, ts);
		}

		Follow_result follow(char const *path, Byte_range_ptr const &dst) override
		{
			_log(__func__, " ", path);
			return _fs.follow(_expand(path).string(), dst);
		}

		unsigned num_dirent(const char *path) override
		{
			return _fs.num_dirent(_expand(path).string());
		}

		bool directory(char const *path) override
		{
			return _fs.directory(_expand(path).string());
		}

		bool dir_entry_exists(const char *path) override
		{
			_expanded_path = _expand(path);
			return _fs.dir_entry_exists(_expanded_path.string());
		}
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		using Fs = Vfs_audit::File_system;

		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &, Node const &config) override
		{
			return { *this, { *new (env.alloc()) Fs(env, config) } };
		}

		void _free(Instance &instance) override { instance.fs.destruct(); };
	};

	static Factory f;
	return &f;
}
