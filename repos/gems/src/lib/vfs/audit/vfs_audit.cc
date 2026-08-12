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

#include <vfs/file_system_factory.h>
#include <vfs/vfs_handle.h>
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

		Vfs::File_system &_root_dir;

		Absolute_path const _audit_path;

		Absolute_path _expanded_path { }; /* buffer for 'dir_entry_exists' return value */

		/**
		 * Expand a path to lay within the audit path
		 */
		Absolute_path _expand(char const *path)
		{
			return Absolute_path(path+1, _audit_path.string());
		}

		struct Handle final : Vfs_handle
		{
			Handle(Handle const &);
			Handle &operator = (Handle const &);

			Log &_audit_log;
			Absolute_path const path;
			Vfs_handle &audited;

			void _log(auto &&... args) { _audit_log.log(args...); }

			Handle(Vfs_audit::File_system &fs, Allocator &alloc,
			       int flags, char const *path, Log &log, Vfs_handle &audited)
			:
				Vfs_handle(fs, alloc, flags), _audit_log(log), path(path), audited(audited)
			{ }

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

			Ftruncate_result ftruncate(file_size len) override
			{
				_log(__func__, " ", path, " ", len);
				return audited.ftruncate(len);
			}

			Sync_result sync() override
			{
				Sync_result const result = audited.sync();

				if (result == Sync_result::RETRY) _log("syncing ", path);
				if (result == Sync_result::OK)    _log("synced ", path);

				return result;
			}
		};

	public:

		File_system(Vfs::Env &env, Node const &config)
		:
			_audit_log(env.env(), config.attribute_value("label", String<64>("audit")).string()),
			_root_dir(env.root_dir()),
			_audit_path(config.attribute_value(
				"path", String<Absolute_path::capacity()>()))
		{ }

		const char* type() override { return "audit"; }

		/***********************
		 ** Directory service **
		 ***********************/

		Dataspace_capability dataspace(const char *path) override
		{
			_log(__func__, " ", path);
			return _root_dir.dataspace(_expand(path).string());
		}

		void release(char const *path, Dataspace_capability ds) override
		{
			_log(__func__, " ", path);
			return _root_dir.release(_expand(path).string(), ds);
		}

		Open_result open(const char *path, unsigned int mode, Vfs::Vfs_handle **out, Allocator &alloc) override
		{
			_log(__func__, " ", path, " ", Hex(mode, Hex::OMIT_PREFIX, Hex::PAD));

			Vfs_handle *audited = nullptr;
			Open_result r = _root_dir.open(_expand(path).string(), mode, &audited, alloc);

			if (!audited || r != OPEN_OK)
				return r;

			try { *out = new (alloc) Handle(*this, alloc, mode, path, _audit_log, *audited); }
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM;  }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
			return r;
		}

		Opendir_result opendir(char const *path, bool create,
	                               Vfs_handle **out, Allocator &alloc) override
		{
			_log(__func__, " ", path, create ? " create " : "");

			Vfs_handle *audited = nullptr;
			Opendir_result r = _root_dir.opendir(_expand(path).string(), create, &audited, alloc);

			if (!audited || r != OPENDIR_OK)
				return r;

			try { *out = new (alloc) Handle(*this, alloc, 0, path, _audit_log, *audited); }
			catch (Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM;  }
			catch (Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }
			return r;
		}

		void close(Vfs::Vfs_handle *vfs_handle) override
		{
			Handle *h = static_cast<Handle*>(vfs_handle);
			_log(__func__, " ", h->path);
			if (h) {
				h->audited.ds().close(&h->audited);
				destroy(h->alloc(), h);
			}
		}

		Stat_result stat(const char *path, Vfs::Directory_service::Stat &buf) override
		{
			_log(__func__, " ", path);
			return _root_dir.stat(_expand(path).string(), buf);
		}

		Unlink_result unlink(const char *path) override
		{
			_log(__func__, " ", path);
			return _root_dir.unlink(_expand(path).string());
		}

		Rename_result rename(const char *from , const char *to) override
		{
			_log(__func__, " ", from, " ", to);
			return _root_dir.rename(_expand(from).string(), _expand(to).string());
		}

		unsigned num_dirent(const char *path) override
		{
			return _root_dir.num_dirent(_expand(path).string());
		}

		bool directory(char const *path) override
		{
			return _root_dir.directory(_expand(path).string());
		}

		bool dir_entry_exists(const char *path) override
		{
			_expanded_path = _expand(path);
			return _root_dir.dir_entry_exists(_expanded_path.string());
		}
};


extern "C" Genode::Vfs::File_system_factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system_factory
	{
		Vfs::File_system *create(Vfs::Env &env, Vfs::Parent_fs &, Node const &config) override
		{
			return new (env.alloc()) Vfs_audit::File_system(env, config);
		}
	};

	static Factory f;
	return &f;
}
