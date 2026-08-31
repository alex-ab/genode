/*
 * \brief  Glyphs file system
 * \author Norman Feske
 * \date   2018-03-26
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _GLYPHS_FILE_SYSTEM_H_
#define _GLYPHS_FILE_SYSTEM_H_

/* Genode includes */
#include <util/callable.h>
#include <vfs/single_file_system.h>
#include <nitpicker_gfx/text_painter.h>

/* gems includes */
#include <gems/vfs_font.h>

namespace Vfs_glyphs {

	using namespace Genode;
	using namespace Genode::Vfs;

	class File_system;

	using Font         = Text_painter::Font;
	using Glyph_header = Vfs_font::Glyph_header;
}


class Vfs_glyphs::File_system : public Single_file_system
{
	public:

		struct Accessor : Interface
		{
			using With_font = Callable<void, Font const &>;

			virtual void _with_font(With_font::Ft const &) = 0;

			void with_font(auto const &fn) { this->_with_font( With_font::Fn { fn } ); }
		};

	private:

		static constexpr unsigned  UNICODE_MAX = 0x10ffff;

		static constexpr file_size FILE_SIZE = Vfs_font::GLYPH_SLOT_BYTES*(UNICODE_MAX + 1);

		Accessor &_accessor;

		struct Vfs_handle : Single_vfs_handle
		{
			Font const &_font;

			Vfs_handle(Directory_service &ds,
			           Allocator         &alloc,
			           Font        const &font)
			:
				Single_vfs_handle(ds, alloc, 0), _font(font)
			{ }

			Read_result read(At const at, Byte_range_ptr const &dst) override
			{
				if (at.pos > FILE_SIZE)
					return Read_error::DENIED;

				Codepoint const codepoint { uint32_t(at.pos / Vfs_font::GLYPH_SLOT_BYTES) };

				size_t out_count = 0;
				size_t byte_offset = size_t(at.pos % Vfs_font::GLYPH_SLOT_BYTES);

				char  *dst_ptr = dst.start;
				size_t count   = dst.num_bytes;

				_font.apply_glyph(codepoint, [&] (Glyph_painter::Glyph const &glyph) {

					if (byte_offset < sizeof(Glyph_header)) {

						Glyph_header const header(glyph);

						char const * const src = (char const *)&header + byte_offset;
						size_t       const len = min(sizeof(header) - byte_offset, count);
						memcpy(dst_ptr, src, len);

						dst_ptr     += len;
						byte_offset += len;
						count       -= len;
						out_count   += len;
					}

					/*
					 * Given that 'byte_offset' is at least 'sizeof(header)',
					 * continue working with 'alpha_offset', which is the first
					 * offset of interest within the array of alpha values.
					 */
					size_t const alpha_offset = (size_t)byte_offset - sizeof(Glyph_header);
					size_t const alpha_values_len = 4*glyph.width*glyph.height;

					if (alpha_offset < alpha_values_len) {
						char const * const src = (char const *)glyph.values + alpha_offset;
						size_t const len = min(alpha_values_len - alpha_offset, count);
						memcpy(dst_ptr, src, len);
						out_count += len;
					}
				});

				return out_count;
			}

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return false; }
		};

	public:

		static char const *type_name() { return "glyphs"; }

		File_system(Parent_fs &parent_fs, Accessor &accessor)
		:
			Single_file_system(parent_fs, {
				.ident = type_name(),
				.name  = type_name(),
				.rwx   = File::RO
			}),
			_accessor(accessor)
		{ }

		void notify_watchers() { Single_file_system::_notify_watchers(); }

		Open_result open(char const  *path, unsigned,
		                 Vfs::Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			try {
				bool font_exists = false;
				_accessor.with_font([&] (Font const &font) {
					font_exists = true;
					*out_handle = new (alloc) Vfs_handle(*this, alloc, font);
				});
				if (!font_exists)
					error("Vfs_glyphs: font not available");
				return OPEN_OK;
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			Stat_result result = Single_file_system::stat(path, out);
			out.size = FILE_SIZE;
			return result;
		}
};

#endif /* _GLYPHS_FILE_SYSTEM_H_ */
