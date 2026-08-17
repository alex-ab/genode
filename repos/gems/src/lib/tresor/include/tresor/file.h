/*
 * \brief  Tresor-local utilities for accessing VFS files
 * \author Martin Stein
 * \date   2020-10-29
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _TRESOR__FILE_H_
#define _TRESOR__FILE_H_

/* base includes */
#include <util/string.h>

/* os includes */
#include <vfs/vfs_handle.h>
#include <vfs/simple_env.h>

/* tresor includes */
#include <tresor/assertion.h>

namespace Tresor {

	using namespace Genode;

	using Path = String<128>;

	template <typename> class File;
	template <typename> class Read_write_file;
	template <typename> class Write_only_file;

	inline Vfs::Vfs_handle &open_file(Vfs::Env &env, Tresor::Path const &path, Vfs::Directory_service::Open_mode mode)
	{
		using Open_result = Vfs::Directory_service::Open_result;
		Vfs::Vfs_handle *handle { nullptr };
		ASSERT(env.fs().open(path.string(), mode, &handle, env.alloc()) == Open_result::OPEN_OK);
		return *handle;
	}
}


template <typename HOST_STATE>
class Tresor::File
{
	private:

		enum State { IDLE, SYNC, READ, WRITE };

		Vfs::Env *_env { };
		Tresor::Path const *_path { };
		HOST_STATE &_host_state;
		State _state { IDLE };
		Vfs::Vfs_handle &_handle;
		size_t _num_processed_bytes { 0 };

		/*
		 * Noncopyable
		 */
		File(File const &) = delete;
		File &operator = (File const &) = delete;

	public:

		File(HOST_STATE &host_state, Vfs::Vfs_handle &handle) : _host_state(host_state), _handle(handle) { }

		File(HOST_STATE &host_state, Vfs::Env &env, Tresor::Path const &path, Vfs::Directory_service::Open_mode mode)
		: _env(&env), _path(&path), _host_state(host_state), _handle(open_file(*_env, *_path, mode)) { }

		~File()
		{
			ASSERT(_state == IDLE);
			if (_env)
				_env->fs().close(&_handle);
		}

		void read(HOST_STATE succeeded, HOST_STATE failed, Vfs::file_size off, Byte_range_ptr dst, bool &progress)
		{
			switch (_state) {
			case IDLE:

				_num_processed_bytes = 0;
				_state = READ;
				progress = true;

				[[fallthrough]];

			case READ:
				{
					Byte_range_ptr curr_dst { dst.start     + _num_processed_bytes,
					                          dst.num_bytes - _num_processed_bytes };
					Vfs::At const at { .pos = off + _num_processed_bytes };

					Vfs::Vfs_handle::Read_result result = _handle.read(at, curr_dst);
					if (result == Vfs::Vfs_handle::Read_error::RETRY)
						break;

					progress = true;
					result.with_result(
						[&] (size_t num_bytes) {
							_num_processed_bytes += num_bytes;
							ASSERT(_num_processed_bytes <= dst.num_bytes);
							if (_num_processed_bytes == dst.num_bytes) {
								_host_state = succeeded;
								_state      = IDLE;
							}
						},
						[&] (Vfs::Vfs_handle::Read_error) {
							error("file: read failed");
							_host_state = failed;
							_state      = IDLE;
						});
					break;
				}
			default: ASSERT_NEVER_REACHED;
			}
		}

		void write(HOST_STATE succeeded, HOST_STATE failed, Vfs::file_size off, Const_byte_range_ptr src, bool &progress)
		{
			switch (_state) {
			case IDLE:

				_num_processed_bytes = 0;
				_state = WRITE;
				progress = true;
				break;

			case WRITE:
			{
				Span curr_src { src.start     + _num_processed_bytes,
				                src.num_bytes - _num_processed_bytes };
				Vfs::At const at { .pos = off + _num_processed_bytes };

				_handle.write(at, curr_src).with_result(
					[&] (size_t num_bytes) {
						_num_processed_bytes += num_bytes;
						if (_num_processed_bytes < src.num_bytes) {
							progress = true;
							return;
						}
						ASSERT(_num_processed_bytes == src.num_bytes);
						_state = IDLE;
						_host_state = succeeded;
						progress = true;
					},
					[&] (Vfs::Vfs_handle::Write_error e) {
						if (e == Vfs::Vfs_handle::Write_error::RETRY)
							return;

						error("file: write failed");
						_host_state = failed;
						_state = IDLE;
						progress = true;
					}
				);
				break;
			}
			default: ASSERT_NEVER_REACHED;
			}
		}

		void sync(HOST_STATE succeeded, HOST_STATE failed, bool &progress)
		{
			switch (_state) {
			case IDLE:
				_state = SYNC;
				progress = true;
				break;

			case SYNC:

				switch (_handle.sync()) {
				case Vfs::Sync_result::RETRY: break;
				case Vfs::Sync_result::OK:

					_state = IDLE;
					_host_state = succeeded;
					progress = true;
					break;

				default:

					error("file: sync failed");
					_host_state = failed;
					_state = IDLE;
					progress = true;
					break;
				}
			default: break;
			}
		}
};

template <typename HOST_STATE>
struct Tresor::Read_write_file : public File<HOST_STATE>
{
	Read_write_file(HOST_STATE &host_state, Vfs::Env &env, Tresor::Path const &path)
	: File<HOST_STATE>(host_state, env, path, Vfs::Directory_service::OPEN_MODE_RDWR) { }
};

template <typename HOST_STATE>
struct Tresor::Write_only_file : public File<HOST_STATE>
{
	Write_only_file(HOST_STATE &host_state, Vfs::Env &env, Tresor::Path const &path)
	: File<HOST_STATE>(host_state, env, path, Vfs::Directory_service::OPEN_MODE_WRONLY) { }
};

#endif /* _TRESOR__FILE_H_ */
