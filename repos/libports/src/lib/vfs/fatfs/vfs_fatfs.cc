/*
 * \brief  FatFS VFS plugin
 * \author Christian Prochaska
 * \author Emery Hemingway
 * \date   2016-05-22
 *
 * See http://www.elm-chan.org/fsw/ff/00index_e.html
 * or documents/00index_e.html in the FatFS source.
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <vfs/vfs_handle.h>
#include <vfs/env.h>
#include <os/path.h>

/* Genode block backend */
#include <fatfs/block.h>

namespace Vfs_fatfs {

/* FatFS includes */
#include <fatfs/ff.h>

	using namespace Genode;
	using namespace Genode::Vfs;
	using namespace Fatfs;

	class File_system;
};


class Vfs_fatfs::File_system : public Vfs::File_system
{
	private:

		using Path = Genode::Path<FF_MAX_LFN>;

		struct Fatfs_file_handle;
		struct Fatfs_dir_handle;
		struct Fatfs_file_watch_handle;
		struct Fatfs_dir_watch_handle;

		using Fatfs_file_handles      = List<Fatfs_file_handle>;
		using Fatfs_dir_watch_handles = List<Fatfs_dir_watch_handle>;
		using Fatfs_watch_handles     = List<Fatfs_file_watch_handle>;

		/**
		 * The FatFS library does not support opening a file
		 * for writing twice, so this plugin manages a tree of
		 * open files shared across open VFS handles.
		 */

		struct File : Avl_node<File>
		{
			Path                path;
			FIL                 fil;
			Fatfs_file_handles  handles;
			Fatfs_watch_handles watchers;

			bool opened() const {
				return (handles.first() || watchers.first()); }

			/************************
			 ** Avl node interface **
			 ************************/

			bool higher(File *other) {
				return (strcmp(other->path.base(), path.base()) > 0); }

			File *lookup(char const *path_str)
			{
				int const cmp = strcmp(path_str, path.base());
				if (cmp == 0)
					return this;

				File *f = Avl_node<File>::child(cmp);
				return f ? f->lookup(path_str) : nullptr;
			}
		};

		struct Fatfs_handle : Vfs_handle
		{
			using Vfs_handle::Vfs_handle;
		};

		struct Fatfs_file_handle : Fatfs_handle, Fatfs_file_handles::Element
		{
			File_system &_fs;
			File *file = nullptr;
			bool modifying = false;

			Fatfs_file_handle(File_system &fs, Allocator &alloc, int status_flags)
			:
				Fatfs_handle(fs, alloc, status_flags), _fs(fs)
			{ }

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (!file) {
					error("Vfs_fatfs: Read_error::DENIED");
					return Read_error::DENIED;
				}
				if ((status_flags()&OPEN_MODE_ACCMODE) == OPEN_MODE_WRONLY)
					return Read_error::DENIED;

				FIL *fil = &file->fil;
				FRESULT fres = f_lseek(fil, at.pos);
				if (fres == FR_OK) {
					UINT bw = 0;
					fres = f_read(fil, dst.start, dst.num_bytes, &bw);
					return bw;
				}
				return Read_error::DENIED;
			}

			Write_result write(At const at, Const_byte_range_ptr const &src) override
			{
				if (!file)        return Write_error::DENIED;
				if (!writeable()) return Write_error::DENIED;

				FRESULT fres = FR_OK;
				FIL *fil = &file->fil;
				FSIZE_t const wpos = at.pos;

				/* seek file pointer */
				if (f_tell(fil) != wpos) {
					/*
					 * seeking beyond the EOF will expand the file size
					 * and is not the expected behavior
					 */
					if (f_size(fil) < wpos)
						return Write_error::DENIED;

					fres = f_lseek(fil, wpos);
					/* check the seek again */
					if (f_tell(fil) != at.pos)
						return Write_error::DENIED;
				}

				if (fres == FR_OK) {
					UINT bw = 0;
					fres = f_write(fil, src.start, src.num_bytes, &bw);
					f_sync(fil);
					modifying = true;
					return bw;
				}

				if (fres == FR_TIMEOUT)
					return Write_error::RETRY;

				return Write_error::DENIED;
			}

			Ftruncate_result ftruncate(file_size len) override
			{
				if (!file)        return FTRUNCATE_ERR_NO_PERM;
				if (!writeable()) return FTRUNCATE_ERR_NO_PERM;

				FIL *fil = &file->fil;
				FRESULT res = FR_OK;

				/* f_lseek will expand a file... */
				res = f_lseek(fil, len);
				if (f_tell(fil) != len)
					return f_size(fil) < len ?
						FTRUNCATE_ERR_NO_SPACE : FTRUNCATE_ERR_NO_PERM;

				/* ... otherwise truncate will shorten to the seek position */
				if ((res == FR_OK) && (len < f_size(fil)))
					res = f_truncate(fil);

				modifying = true;

				return res == FR_OK ?
					FTRUNCATE_OK : FTRUNCATE_ERR_NO_PERM;
			}

			bool read_ready() const override { return true; }

			bool write_ready() const override
			{
				/*
				 * Wakeup from WRITE_ERR_WOULD_BLOCK not supported.
				 */
				return true;
			}

			/**
			 * Notify other handles if this handle has modified its file.
			 *
			 * Files are flushed to blocks after every write.
			 */
			Sync_result sync() override
			{
				if (file && modifying) {
					modifying = false;
					file->handles.remove(this);
					_fs._notify(*file);
					file->handles.insert(this);
				}
				return Sync_result::OK;
			}
		};

		struct Fatfs_dir_handle : Fatfs_handle
		{
			file_size cur_index = 0;
			Path const path;
			DIR dir;

			Fatfs_dir_handle(File_system &fs, Allocator &alloc, char const *path)
			:
				Fatfs_handle(fs, alloc, 0), path(path)
			{ }

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				/* not very efficient, just N calls to f_readdir */

				if (dst.num_bytes < sizeof(Dirent))
					return Read_error::DENIED;

				size_t dir_index = size_t(at.pos / sizeof(Dirent));
				if (dir_index < cur_index) {
					/* reset the seek position */
					f_readdir(&dir, nullptr);
					cur_index = 0;
				}

				Dirent &vfs_dirent = *(Dirent*)dst.start;

				FILINFO info;
				FRESULT res;

				while (cur_index <= dir_index) {
					res = f_readdir (&dir, &info);
					if ((res != FR_OK) || (!info.fname[0])) {
						f_readdir(&dir, nullptr);
						cur_index = 0;

						vfs_dirent = {
							.type = Dirent_type::END,
							.rwx  = Node_rwx::rwx(),
							.name = { }
						};
						return sizeof(Dirent);
					}
					cur_index++;
				}

				vfs_dirent = {
					.type = (info.fattrib & AM_DIR)
					      ? Dirent_type::DIRECTORY
					      : Dirent_type::CONTINUOUS_FILE,
					.rwx  = Node_rwx::rwx(),
					.name = { (char const *)info.fname }
				};
				return sizeof(Dirent);
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return false; }
		};

		Vfs::Env  &_vfs_env;
		Parent_fs &_parent_fs;

		FATFS _fatfs;

		/* Tree of open FatFS file objects */
		Avl_tree<File> _open_files;

		/* Pre-allocated FIL */
		File *_next_file = nullptr;

		/**
		 * Return an open FatFS file matching path or null.
		 */
		File *_opened_file(char const *path)
		{
			return _open_files.first() ?
				_open_files.first()->lookup(path) : nullptr;
		}

		/**
		 * Notify the application for each handle on a given file.
		 */
		void _notify(File &file)
		{
			_parent_fs.notify_watchers(Span::from_cstring(file.path.base()));
		}

		/**
		 * Notify the application for each handle on the parent
		 * directory of a given path.
		 */
		void _notify_parent_of(char const *path)
		{
			with_compound_dir(Span::from_cstring(path), [&] (Span const &dir_path) {
				_parent_fs.notify_watchers(dir_path); });
		}

		/**
		 * Close an open FatFS file
		 */
		void _close(File &file)
		{
			/* close file */
			_open_files.remove(&file);
			f_close(&file.fil);

			if (_next_file == nullptr) {
				/* reclaim heap space */
				file.path.import("");
				_next_file = &file;
			} else {
				destroy(_vfs_env.alloc(), &file);
			}
		}

		/**
		 * Invalidate all handles on a FatFS file
		 * and close the file
		 */
		void _close_all(File &file)
		{
			/* invalidate handles */
			for (auto *handle = file.handles.first();
			     handle; handle = file.handles.first())
			{
				handle->file = nullptr;
				file.handles.remove(handle);
			}

			_close(file);
		}

	public:

		File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &config)
		:
			_vfs_env(env), _parent_fs(parent_fs)
		{
			{
				if (f_setcp(0) != FR_OK) {
					error("failed to set codepage to 0");
					throw FR_INVALID_PARAMETER;
				}
			}
			/* mount the file system */
			String<4> const drive_num { "0" };
			switch (f_mount(&_fatfs, (const TCHAR*)drive_num.string(), 1)) {
			case FR_OK: {
				TCHAR label[24] = { '\0' };
				f_getlabel((const TCHAR*)drive_num.string(), label, nullptr);
				log("FAT file system \"", (char const *)label, "\" mounted");
				return;
			}
			case FR_INVALID_DRIVE:
				error("invalid drive ", drive_num);           throw ~0;
			case FR_DISK_ERR:
				error("drive ", drive_num, " disk error");    throw ~0;
			case FR_NOT_READY:
				error("drive ", drive_num, " not ready");     throw ~0;
			case FR_NO_FILESYSTEM:
				error("no file system on drive ", drive_num); throw ~0;
			default:
				error("failed to mount drive ", drive_num);   throw ~0;
			}
		}

		char const *type() override { return "fatfs"; }

		Open_result open(char const *path, unsigned vfs_mode,
		                 Vfs_handle **vfs_handle,
		                 Allocator  &alloc) override
		{
			Fatfs_file_handle *handle;

			File *file = _opened_file(path);

			bool create = vfs_mode & OPEN_MODE_CREATE;

			if (file && create) {
				return OPEN_ERR_EXISTS;
			}

			if (file && f_error(&file->fil)) {
				error("FatFS: hard error on file '", path, "'");
				return OPEN_ERR_NO_PERM;
			};

			/* attempt allocation before modifying blocks */
			if (!_next_file)
				_next_file = new (_vfs_env.alloc()) File();
			handle = new (alloc) Fatfs_file_handle(*this, alloc, vfs_mode);

			if (!file) {
				file = _next_file;
				FRESULT fres = f_open(
					&_next_file->fil, (TCHAR const *)path,
					FA_READ | FA_WRITE | (create ? FA_CREATE_NEW : FA_OPEN_EXISTING));
				if (fres != FR_OK) {
					destroy(alloc, handle);
					switch(fres) {
					case FR_NO_FILE:
					case FR_NO_PATH:      return OPEN_ERR_UNACCESSIBLE;
					case FR_EXIST:        return OPEN_ERR_EXISTS;
					case FR_INVALID_NAME: return OPEN_ERR_NAME_TOO_LONG;
					default:              return OPEN_ERR_NO_PERM;
					}
				}

				file->path.import(path);
				_open_files.insert(file);
				_next_file = nullptr;
			}

			if (create)
				_notify_parent_of(path);

			file->handles.insert(handle);
			handle->file = file;
			*vfs_handle = handle;
			return OPEN_OK;
		}

		Opendir_result opendir(char const *path, bool create,
		                       Vfs_handle **vfs_handle,
		                       Allocator &alloc) override
		{
			Fatfs_dir_handle *handle;

			/* attempt allocation before modifying blocks */
			handle = new (alloc) Fatfs_dir_handle(*this, alloc, path);

			if (create) {
				FRESULT res = f_mkdir((const TCHAR*)path);
				if (res != FR_OK) {
					destroy(alloc, handle);
					switch (res) {
					case FR_EXIST:        return OPENDIR_ERR_NODE_ALREADY_EXISTS;
					case FR_NO_PATH:      return OPENDIR_ERR_LOOKUP_FAILED;
					case FR_INVALID_NAME: return OPENDIR_ERR_NAME_TOO_LONG;
					default:              return OPENDIR_ERR_PERMISSION_DENIED;
					}
				}
			}

			FRESULT res = f_opendir(&handle->dir, (const TCHAR*)path);
			if (res != FR_OK) {
				destroy(alloc, handle);
				switch (res) {
				case FR_NO_PATH: return OPENDIR_ERR_LOOKUP_FAILED;
				default:         return OPENDIR_ERR_PERMISSION_DENIED;
				}
			}

			*vfs_handle = handle;

			return OPENDIR_OK;
		}

		void close(Vfs_handle *vfs_handle) override
		{
			{
				auto *handle = dynamic_cast<Fatfs_file_handle *>(vfs_handle);
				bool notify = false;

				if (handle) {
					File *file = handle->file;
					if (file) {
						file->handles.remove(handle);
						if (file->opened()) {
							notify = handle->modifying;
						} else {
							_close(*file);
						}
					}
					destroy(handle->alloc(), handle);

					if (notify)
						_notify(*file);
					return;
				}
			}

			{
				auto *handle = dynamic_cast<Fatfs_dir_handle *>(vfs_handle);

				if (handle) {
					f_closedir(&handle->dir);
					destroy(handle->alloc(), handle);
				}
			}
		}

		Dataspace_capability dataspace(char const *path) override
		{
			warning(__func__, " not implemented in FAT plugin");
			return Dataspace_capability();
		}

		void release(char const *path, Dataspace_capability ds_cap) override { }

		unsigned num_dirent(char const *path) override
		{
			DIR      dir;
			FILINFO  fno;
			unsigned count = 0;

			if (f_opendir(&dir, (const TCHAR*)path) != FR_OK) return 0;

			fno.fname[0] = 0xFF;
			while ((f_readdir (&dir, &fno) == FR_OK) && fno.fname[0])
				++count;
			f_closedir(&dir);
			return count;
		}

		bool directory(char const *path) override
		{
			if (path[0] == '/' && path[1] == '\0') return true;

			FILINFO fno;

			return f_stat((const TCHAR*)path, &fno) == FR_OK ?
				(fno.fattrib & AM_DIR) : false;
		}

		bool dir_entry_exists(char const *path) override
		{
			FILINFO fno;

			if (_opened_file(path))
				return true;
			else
				return f_stat((const TCHAR*)path, &fno) == FR_OK;
		}

		Stat_result stat(char const *path, Stat &stat) override
		{
			stat = Stat { };

			FILINFO info;

			FRESULT const err = f_stat((const TCHAR*)path, &info);
			switch (err) {
			case FR_OK:
				stat.device = (addr_t)this;
				stat.type   = (info.fattrib & AM_DIR)
				            ? Node_type::DIRECTORY
				            : Node_type::CONTINUOUS_FILE;
				stat.rwx    = Node_rwx::rwx();

				/* XXX: size in f_stat is always zero */
				if ((stat.type == Node_type::CONTINUOUS_FILE) && (info.fsize == 0)) {
					File *file = _opened_file(path);
					if (file) {
						stat.size = f_size(&file->fil);
					} else {
						FIL fil;
						if (f_open(&fil, (TCHAR const *)path, FA_READ) == FR_OK) {
							stat.size = f_size(&fil);
							f_close(&fil);
						}
					}
				} else {
					stat.size = info.fsize;
				}
				return STAT_OK;

			case FR_NO_FILE:
			case FR_NO_PATH:
				return STAT_ERR_NO_ENTRY;

			default:
				error("unhandled FatFS::f_stat error ", (int)err);
				return STAT_ERR_NO_PERM;
			}
			return STAT_ERR_NO_PERM;
		}

		Unlink_result unlink(char const *path) override
		{
			/* close the file if it is open */
			if (File *file = _opened_file(path)) {
				_notify(*file);
				_close_all(*file);
			}

			switch (f_unlink((const TCHAR*)path)) {
			case FR_OK: break;
			case FR_NO_FILE:
			case FR_NO_PATH: return UNLINK_ERR_NO_ENTRY;
			default:         return UNLINK_ERR_NO_PERM;
			}

			_notify_parent_of(path);
			return UNLINK_OK;
		}

		Rename_result rename(char const *from, char const *to) override
		{
			if (File *to_file = _opened_file(to)) {
				_notify(*to_file);
				_close_all(*to_file);
				f_unlink((TCHAR const *)to);
			} else {
				FILINFO info;
				if (FR_OK == f_stat((TCHAR const *)to, &info)) {
					if (info.fattrib & AM_DIR) {
						return RENAME_ERR_NO_PERM;
					} else {
						f_unlink((TCHAR const *)to);
					}
				}
			}

			if (File *from_file = _opened_file(from)) {
				_notify(*from_file);
				_close_all(*from_file);
			}

			switch (f_rename((const TCHAR*)from, (const TCHAR*)to)) {
			case FR_OK: break;
			case FR_NO_FILE:
			case FR_NO_PATH: return RENAME_ERR_NO_ENTRY;
			default:         return RENAME_ERR_NO_PERM;
			}

			_notify_parent_of(from);
			if (strcmp(from, to) != 0)
				_notify_parent_of(to);
			return RENAME_OK;
		}
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		Vfs::File_system *create(Vfs::Env &vfs_env, Vfs::Parent_fs &parent_fs,
		                         Genode::Node const &node) override
		{
			Fatfs::block_init(vfs_env.env(), vfs_env.alloc());
			return new (vfs_env.alloc()) Vfs_fatfs::File_system(vfs_env, parent_fs, node);
		}
	};

	static Factory factory;
	return &factory;
}
