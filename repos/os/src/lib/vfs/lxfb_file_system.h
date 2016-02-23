/*
 * \brief  Linux framebuffer device file system
 * \author Martin Stein
 * \date   2016-02-09
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__LXFB_FILE_SYSTEM_H_
#define _INCLUDE__VFS__LXFB_FILE_SYSTEM_H_

/* Genode includes */
#include <vfs/single_file_system.h>
#include <framebuffer_session/connection.h>
#include <vfs/lxfb.h>

namespace Vfs { class Lxfb_file_system; }


class Vfs::Lxfb_file_system : public Single_file_system
{
	private:

		enum {
			LXFB_WIDTH  = 640,
			LXFB_HEIGHT = 480,
			LXFB_BPP    = 32,
		};

		Vfs::Env                &_env;
		Framebuffer::Connection  _fb { _env.env(), Framebuffer::Mode() };

		Ioctl_result _fbioget_fscreeninfo(Ioctl_arg arg)
		{
			using Genode::size_t;
			Framebuffer::Mode fb_mode = _fb.mode();
			size_t fb_line_sz = fb_mode.width() * fb_mode.bytes_per_pixel();
			size_t fb_sz      = fb_line_sz * fb_mode.height();

			fb_fix_screeninfo * fixinfo = (fb_fix_screeninfo *)arg;
			fixinfo->smem_start  = 0;
			fixinfo->smem_len    = fb_sz;
			fixinfo->line_length = fb_line_sz;
			return IOCTL_OK;
		}

		Ioctl_result _fbioget_vscreeninfo(Ioctl_arg arg)
		{
			Framebuffer::Mode fb_mode = _fb.mode();
			fb_var_screeninfo * varinfo = (fb_var_screeninfo *)arg;
			varinfo->xres = fb_mode.width();
			varinfo->yres = fb_mode.height();
			varinfo->xoffset = 0;
			varinfo->yoffset = 0;
			varinfo->bits_per_pixel = fb_mode.bytes_per_pixel() * 8;
			switch (fb_mode.format()) {
			case Framebuffer::Mode::RGB565:
				varinfo->blue.offset  = 0;
				varinfo->green.offset = 5;
				varinfo->red.offset   = 11;
				break;
			default:
				Genode::error("invalid framebuffer format");
				return IOCTL_ERR_INVALID;
			}
			return IOCTL_OK;
		}

		Lxfb_file_system(Lxfb_file_system const &);
		Lxfb_file_system &operator = (Lxfb_file_system const &);

		struct Lxfb_vfs_handle : Single_vfs_handle
		{
			Lxfb_vfs_handle(Directory_service &ds,
			                File_io_service   &fs,
			                Genode::Allocator &alloc)
			: Single_vfs_handle(ds, fs, alloc, 0) { }


			Read_result read(char *, file_size, file_size &) override
			{
				Genode::warning("Not implemented");
				return READ_ERR_INVALID;
			}

			Write_result write(char const *, file_size, file_size &) override
			{
				Genode::warning("Not implemented");
				return WRITE_ERR_INVALID;
			}

			bool read_ready() override { return true; }
		};

	public:

		Lxfb_file_system(Vfs::Env &env, Genode::Xml_node config)
		:
			Single_file_system { Node_type::TRANSACTIONAL_FILE, name(), Node_rwx::rw(), config },
			_env               { env }
		{ }

		static char const *name()   { return "lxfb"; }
		char const *type() override { return "lxfb"; }

		Open_result open(char const  *path, unsigned,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				*out_handle = new (alloc)
					Lxfb_vfs_handle(*this, *this, alloc);
				return OPEN_OK;
			}
			catch (Genode::Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Genode::Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}

		Ioctl_result ioctl(Vfs_handle *, Ioctl_opcode opcode,
		                   Ioctl_arg arg, Ioctl_out &) override
		{
			switch (opcode) {
			case IOCTL_OP_FBIOGET_VSCREENINFO: return _fbioget_vscreeninfo(arg);
			case IOCTL_OP_FBIOGET_FSCREENINFO: return _fbioget_fscreeninfo(arg);
			default: Genode::warning("invalid ioctl request", (int)opcode); break;
			}
			return IOCTL_ERR_INVALID;
		}

		Mmap_type mmap_type() override { return MMAP_DIRECT; }

		Dataspace_capability mmap_direct_ds() override { return _fb.dataspace(); }
};

#endif /* _INCLUDE__VFS__LXFB_FILE_SYSTEM_H_ */
