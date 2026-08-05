/*
 * \brief  Truetype font file system
 * \author Norman Feske
 * \date   2018-03-07
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <vfs/dir_file_system.h>
#include <vfs/readonly_value_file_system.h>
#include <os/vfs.h>

/* gems includes */
#include <gems/ttf_font.h>
#include <gems/cached_font.h>

/* local includes */
#include <glyphs_file_system.h>

namespace Vfs_ttf {

	using namespace Genode;
	using namespace Genode::Vfs;

	class Font_from_file;
	class File_system;

	using Font = Text_painter::Font;
}


struct Vfs_ttf::Font_from_file
{
	using Path = Directory::Path;

	Directory    const _dir;
	File_content const _content;

	Constructible<Ttf_font const> _font { };

	/*
	 * Each slot of the glyphs file is 64 KiB, which limits the maximum glyph
	 * size to 128x128. We cap the size at 100px to prevent cut-off glyphs.
	 */
	static constexpr float MAX_SIZE_PX = 100.0;

	Font_from_file(Vfs::Env &vfs_env, Path const &file_path, float px)
	:
		_dir(vfs_env),
		_content(vfs_env.alloc(), _dir, file_path, File_content::Limit{10*1024*1024})
	{
		_content.bytes([&] (char const *ptr, size_t) {
			_font.construct(vfs_env.alloc(), ptr, min(px, MAX_SIZE_PX)); });
	}

	Font const &font() const { return *_font; }
};


struct Vfs_ttf::File_system : Dir_file_system, File_system_factory, Watch_handle::Handler
{
	Vfs::Env &_env;

	struct Font_config
	{
		Directory::Path    path;
		float              size;
		Cached_font::Limit cache_limit;

		static Font_config from_node(Node const &node)
		{
			return {
				.path        = node.attribute_value("path", Directory::Path()),
				.size        = (float)node.attribute_value("size_px", 16.0d),
				.cache_limit = { node.attribute_value("cache", Number_of_bytes()) }
			};
		}

		bool operator != (Font_config const &other) const
		{
			return path != other.path || size != other.size
			    || cache_limit.value != other.cache_limit.value;
		}
	} _font_config;

	struct Font
	{
		Font_from_file     font;
		Cached_font::Limit cache_limit;
		Cached_font        cached_font;

		Font(Vfs::Env &env, Font_config &config)
		:
			font(env,
			     config.path,
			     config.size),
			cache_limit(config.cache_limit),
			cached_font(env.alloc(), font.font(), cache_limit)
		{ }
	};

	Reconstructible<Font> _font;

	Vfs_glyphs::File_system _glyphs_fs { *this, _font->cached_font };

	Readonly_value_file_system<unsigned> _baseline_fs   { *this, "baseline",   0 };
	Readonly_value_file_system<unsigned> _height_fs     { *this, "height",     0 };
	Readonly_value_file_system<unsigned> _max_width_fs  { *this, "max_width",  0 };
	Readonly_value_file_system<unsigned> _max_height_fs { *this, "max_height", 0 };

	Watch_handle _watch_handle;

	void _update_attributes()
	{
		_baseline_fs  .value(_font->font.font().baseline());
		_height_fs    .value(_font->font.font().height());
		_max_width_fs .value(_font->font.font().bounding_box().w);
		_max_height_fs.value(_font->font.font().bounding_box().h);
	}

	Vfs::File_system *create(Vfs::Env &, Parent_fs &, Node const &node) override
	{
		if (node.has_type(Vfs_glyphs::File_system::type_name()))
			return &_glyphs_fs;

		if (node.has_type(Readonly_value_file_system<unsigned>::type_name()))
			return _baseline_fs.matches(node)   ? &_baseline_fs
			     : _height_fs.matches(node)     ? &_height_fs
			     : _max_width_fs.matches(node)  ? &_max_width_fs
			     : _max_height_fs.matches(node) ? &_max_height_fs
			     : nullptr;

		return nullptr;
	}

	void update(Node const &config, File_system_factory &) override
	{
		Font_config const orig = _font_config;
		_font_config = Font_config::from_node(config);
		_font.construct(_env, _font_config);
		_update_attributes();

		if (orig != _font_config)
			_glyphs_fs.notify_watchers();
	}

	/**
	 * Watch_handle::Handler interface
	 */
	void io_handle_watch() override
	{
		/* called whenever the TTF input file changes */

		_font.construct(_env, _font_config);
		_update_attributes();
		_glyphs_fs.notify_watchers();
	}

	using Config = String<200>;
	static Config _config(Node const &node)
	{
		char buf[Config::capacity()] { };

		Generator::generate({ buf, sizeof(buf) }, "dir", [&] (Generator &g) {
			using Name = String<64>;
			g.attribute("name", node.attribute_value("name", Name()));
			g.node("glyphs");
			g.node("readonly_value", [&] { g.attribute("name", "baseline");   });
			g.node("readonly_value", [&] { g.attribute("name", "height");     });
			g.node("readonly_value", [&] { g.attribute("name", "max_width");  });
			g.node("readonly_value", [&] { g.attribute("name", "max_height"); });
		}).with_error([] (Buffer_error) {
			warning("VFS-TTF compound exceeds maximum buffer size");
		});
		return Config(Cstring(buf));
	}

	File_system(Vfs::Env &vfs_env, Parent_fs &parent_fs, Node const &node)
	:
		Dir_file_system(vfs_env, parent_fs, Node(_config(node))),
		_env(vfs_env),
		_font_config(Font_config::from_node(node)),
		_font(vfs_env, _font_config),
		_watch_handle(vfs_env.watch_handles(), vfs_env.root_dir(), _font_config.path, *this)
	{
		Dir_file_system::update(Node(_config(node)), *this);
	}

	char const *type() override { return "ttf"; }
};


/**************************
 ** VFS plugin interface **
 **************************/

extern "C" Genode::Vfs::File_system_factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system_factory
	{
		Vfs::File_system *create(Vfs::Env &vfs_env, Vfs::Parent_fs &parent_fs,
		                         Node const &node) override
		{
			try { return new (vfs_env.alloc())
				Vfs_ttf::File_system(vfs_env, parent_fs, node); }
			catch (...) { }
			return nullptr;
		}
	};

	static Factory factory;
	return &factory;
}
