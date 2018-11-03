/*
 * \brief  VDI file as a Block session
 * \author Josef Soentgen
 * \date   2018-11-01
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* Genode includes */
#include <base/attached_ram_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <block/component.h>
#include <block/driver.h>
#include <util/string.h>
#include <vfs/simple_env.h>

#include <base/debug.h>
#include <vfs/print.h>

/* local includes */
#include <vdi_types.h>

namespace Vdi {
	struct Block;
	struct Meta_data;
} /* namespace Vdi */


struct Vdi::Block
{
	uint32_t value;

	enum {
		BLOCK_FREE = ~0u,
		BLOCK_ZERO = ~1u,
	};

	bool zero()      const { return value == BLOCK_ZERO; }
	bool free()      const { return value == BLOCK_FREE; }
	bool allocated() const { return value  < BLOCK_FREE; }
};


struct Vdi::Meta_data
{
	uint32_t const blocks_offset;
	uint32_t const data_offset;

	uint32_t const block_size;
	uint32_t const sector_size;

	Vdi::Block *table            { nullptr };
	uint32_t    max_blocks       { 0 };
	uint32_t    allocated_blocks { 0 };

	int fd { -1 };

	Meta_data(uint32_t blocks, uint32_t data,
	          uint32_t block_size, uint32_t sector_size)
	:
		blocks_offset(blocks), data_offset(data),
		block_size(block_size), sector_size(sector_size)
	{ }
};


#if 0
static void print_uuid(Random_uuid const *uuid)
{
	printf("%8.8x-%4.4x-%4.4x-%2.2x%2.2x-",
	       uuid->dce.time_low, uuid->dce.time_mid,
	       uuid->dce.time_hi_and_version, uuid->dce.clock_seq_hi_and_reserved,
	       uuid->dce.clock_seq_low);
	for (int i = 0; i < 6; i++) {
		printf("%2.2x", uuid->dce.node[i]);
	}
	printf("\n");
}


static void print_block_table(Meta_data const &md)
{
	uint32_t const blocks_offset = md.blocks_offset;
	uint32_t const data_offset   = md.data_offset;
	uint32_t const allocated     = md.allocated_blocks;
	printf("b: %u d: %u a: %u\n", blocks_offset, data_offset, allocated);

	for (uint32_t i = 0; i < allocated; i++) {
		uint32_t const id   = md.table[i].value;
		uint64_t const offset = data_offset + (id * HeaderV1Plus::BLOCK_SIZE);
		printf("  block[%u] offset: %lu\n", i, offset);
	}
}


static void print_sector_offset(Meta_data const &md, uint64_t nr)
{
	uint64_t const offset = lookup_disk_sector(md, nr);
	if (offset == ~0) {
		printf("Not allocated sector %lu\n", nr);
	} else {
		printf("Sector %lu offset: %lu\n", nr, offset);
	}
}


static void print_headers(Preheader const &ph, HeaderV1Plus const &h)
{
	printf("--- PreHeader ---\n");
	printf("Info: '%s'\n", ph.info);
	printf("Signature okay: %s\n", ph.valid() ? "yes" : "no");
	printf("Version: %u.%u\n", ph.major(), ph.minor());
	printf("--- HeaderV1Plus ---\n");
	printf("Size:          %u\n", h.size);
	printf("Type:          %u\n", h.type);
	printf("Flags:         0x%x\n", h.flags);
	printf("Blocks offset: %u\n", h.blocks_offset);
	printf("Data offset:   %u\n", h.data_offset);
	printf("Legacy cylinders:   %u\n", h.legacy_geometry.cylinders);
	printf("Legacy heads:       %u\n", h.legacy_geometry.heads);
	printf("Legacy sectors:     %u\n", h.legacy_geometry.sectors);
	printf("Legacy sector_size: %u\n", h.legacy_geometry.sector_size);
	printf("Disk size:        %lu\n", h.disk_size);
	printf("Block size:       %u\n", h.block_size);
	printf("Block size extra: %u\n", h.block_size_extra);
	printf("Blocks:           %u\n", h.blocks);
	printf("Allocated blocks: %u\n", h.allocated_blocks);
	printf("Image UUID:       "); print_uuid(&h.image_uuid);
	printf("Modify UUID:      "); print_uuid(&h.modify_uuid);
	if (h.prev_uuid.valid()) {
		printf("Prev UUID:        "); print_uuid(&h.prev_uuid);
		printf("Prev modify UUID: "); print_uuid(&h.prev_modify_uuid);
	}
}
#endif


static bool xml_attr_ok(Genode::Xml_node node, char const *attr)
{
	return node.attribute_value(attr, false);
}


static Genode::Xml_node vfs_config(Genode::Xml_node const &config)
{
	try {
		return config.sub_node("vfs");
	} catch (...) {
		Genode::error("VFS not configured");
		throw;
	}
}

namespace Util {

	bool blocking_read(Genode::Entrypoint &ep,
	                   Vfs::Vfs_handle &handle, Vfs::file_size const start,
	                   char *dst, Vfs::file_size const length)
	{
		Vfs::file_size bytes_read = 0;
		Vfs::file_size remaining = length;
		Vfs::file_size offset = start;

		while (bytes_read < length) {

			handle.seek(offset);

			Genode::error(__func__, ":", __LINE__, " bytes_read: ", bytes_read, " remaining: ", remaining, " offset: ", offset);

			while (!handle.fs().queue_read(&handle, remaining)) {
				ep.wait_and_dispatch_one_io_signal();
			}

			char          *p = dst + bytes_read;
			Vfs::file_size n = 0;
			for (;;) {

				Vfs::File_io_service::Read_result read_result =
					handle.fs().complete_read(&handle, p, remaining, n);

				if (read_result != Vfs::File_io_service::READ_QUEUED) { break; }

				ep.wait_and_dispatch_one_io_signal();
			}

			if (!n) {
				Genode::error("could not read file");
				break;
			}

			bytes_read += n;
			remaining  -= n;
			offset     += n;
		}

		return bytes_read == length;
	}
}

class Vdi_block_driver : public Block::Driver
{
	private:

		Vdi_block_driver(Vdi_block_driver const &) = delete;
		Vdi_block_driver operator=(Vdi_block_driver const&) = delete;

		Genode::Env  &_env;
		Genode::Heap  _heap { _env.ram(), _env.rm() };

		Genode::Attached_ram_dataspace  _header_buffer { _env.ram(), _env.rm(), 2u<<20 };
		Vfs::file_size const            _header_size   { _header_buffer.size() };
		char                           *_header_addr   { _header_buffer.local_addr<char>() };

		Genode::Attached_ram_dataspace  _zero_buffer { _env.ram(), _env.rm(), 64u<<10 };
		Vfs::file_size const            _zero_size   { _zero_buffer.size() };
		char                           *_zero_addr   { _zero_buffer.local_addr<char>() };

		Block::sector_t            _block_count {   0 };
		Genode::size_t             _block_size  { 512 };
		Block::Session::Operations _block_ops   { };

		Vfs::Simple_env _vfs_env;

		Vfs::Vfs_handle *_vdi_file { nullptr };

		Genode::Constructible<Vdi::Meta_data> _md { };

		static inline constexpr uint32_t sectors_per_block()
		{
			return HeaderV1Plus::BLOCK_SIZE / HeaderV1Plus::SECTOR_SIZE;
		}

		static inline uint32_t sector_to_block(uint64_t nr)
		{
			return nr / sectors_per_block();
		}

		uint64_t _lookup_disk_sector(uint64_t nr)
		{
			uint32_t const max_bid = _md->max_blocks;
			uint32_t const bid = sector_to_block(nr);
			if (bid >= max_bid || !_md->table[bid].allocated()) { return ~0; }

			uint64_t const pid = _md->table[bid].value;
			uint32_t const dis = (nr % sectors_per_block()) * HeaderV1Plus::SECTOR_SIZE;

			uint32_t const data    = _md->data_offset;
			return (uint64_t)data + (pid * HeaderV1Plus::BLOCK_SIZE) + dis;
		}

		bool _sync_header(uint32_t id)
		{
			HeaderV1Plus *h   = (HeaderV1Plus*)(_header_addr + sizeof(Preheader));
			Vdi::Block *table = (Vdi::Block*)(_header_addr + h->blocks_offset);

			h->allocated_blocks = _md->allocated_blocks;
			table[id].value     = _md->table[id].value;

			using Write_result = Vfs::File_io_service::Write_result;
			Vfs::file_size n = 0;
			Write_result res = Vfs::File_io_service::WRITE_ERR_INVALID;

			/* update block table */
			Vfs::file_offset const offset = h->blocks_offset + (id * sizeof (uint32_t));
			_vdi_file->seek(offset);
			res = _vdi_file->fs().write(_vdi_file, (char const*)&table[id], sizeof(uint32_t), n);
			if (n != sizeof (uint32_t) || res != Vfs::File_io_service::WRITE_OK) { return false; }
			if (_vdi_file->fs().complete_sync(_vdi_file) != Vfs::File_io_service::SYNC_OK) { return false; }

			/* update header */
			_vdi_file->seek(0);
			res = _vdi_file->fs().write(_vdi_file, (char const*)_header_addr,
			                            HeaderV1Plus::SECTOR_SIZE, n);
			if (n != HeaderV1Plus::SECTOR_SIZE || res != Vfs::File_io_service::WRITE_OK) { return false; }
			if (_vdi_file->fs().complete_sync(_vdi_file) != Vfs::File_io_service::SYNC_OK) { return false; }

			return true;
		}

		bool _allocate_block(uint64_t nr)
		{
			if (_md->allocated_blocks >= _md->max_blocks) { return false; }

			uint64_t const offset = _md->data_offset + (_md->allocated_blocks * _md->block_size);

			using Write_result = Vfs::File_io_service::Write_result;
			Vfs::file_size total = 0;
			do {
				Vfs::file_size n = 0;
				_vdi_file->seek(offset + total);
				Write_result res = _vdi_file->fs().write(_vdi_file, _zero_addr,
				                                         _zero_size, n);
				if (n != _zero_size || res != Vfs::File_io_service::WRITE_OK) { return false; }

				total += n;
			} while (total < _md->block_size);

			_vdi_file->fs().complete_sync(_vdi_file);

			uint32_t const bid = sector_to_block(nr);
			_md->table[bid].value = _md->allocated_blocks;
			_md->allocated_blocks++;

			return _sync_header(bid);
		}

	public:

		struct Could_not_open_file : Genode::Exception { };
		struct Io_error            : Genode::Exception { };

		Vdi_block_driver(Genode::Env &env, Genode::Xml_node config)
		:
			Block::Driver(env.ram()), _env(env),
			_vfs_env(_env, _heap, vfs_config(config))
		{
			bool const writeable = xml_attr_ok(config, "writeable");

			_block_ops.set_operation(Block::Packet_descriptor::READ);
			if (writeable) {
				_block_ops.set_operation(Block::Packet_descriptor::WRITE);
			}

			PDBG();

			Genode::String<256> file;
			try {
				config.attribute("file").value(&file);
				Vfs::Directory_service::Open_result open_result =
				_vfs_env.root_dir().open(file.string(),
				                         writeable ? Vfs::Directory_service::OPEN_MODE_RDWR
				                                   : Vfs::Directory_service::OPEN_MODE_RDONLY,
				                         &_vdi_file, _heap);
				if (open_result != Vfs::Directory_service::OPEN_OK) {
					Genode::error("Could not open '", file, "'");
					throw Could_not_open_file();
				}
			} catch (...) {
				Genode::error("mandatory file attribute missing");
				throw Could_not_open_file();
			}

			PDBG();

			bool const success = Util::blocking_read(_env.ep(), *_vdi_file, 0,
			                                         _header_addr, _header_size);
			if (!success) { throw Io_error(); }

			// while (!_vdi_file->fs().queue_read(_vdi_file, _header_size)) {
			// 	_env.ep().wait_and_dispatch_one_io_signal();
			// }

			// Vfs::file_size n = 0;
			// for (;;) {

			// 	Vfs::File_io_service::Read_result read_result =
			// 		_vdi_file->fs().complete_read(_vdi_file, _header_addr, _header_size, n);
			// 	if (read_result != Vfs::File_io_service::READ_QUEUED
			// 	    && n != _header_size) {
			// 		PDBG("n: ", n, " ", read_result);
			// 		throw Io_error(); }

			// 	_env.ep().wait_and_dispatch_one_io_signal();
			// }

			PDBG();

			HeaderV1Plus *h = (HeaderV1Plus*)(_header_addr + sizeof(Preheader));

			uint32_t blocks_offset = h->blocks_offset;
			uint32_t data_offset   = h->data_offset;
			_md.construct(blocks_offset, data_offset,
			                     HeaderV1Plus::BLOCK_SIZE, HeaderV1Plus::SECTOR_SIZE);
			_md->max_blocks       = h->blocks;
			_md->allocated_blocks = h->allocated_blocks;
			_md->table            = (Vdi::Block*)(_header_addr + h->blocks_offset);

			_block_size  = HeaderV1Plus::SECTOR_SIZE;
			_block_count = h->disk_size / _block_size;

			Genode::log("Provide '", file.string(), "' as block device "
			            "block_size: ", _block_size, " block_count: ",
			            _block_count, " writeable: ", writeable ? "yes" : "no");
		}

		~Vdi_block_driver()
		{
			/* XXX close file */
		}


		/*****************************
		 ** Block::Driver interface **
		 *****************************/

		Genode::size_t      block_size() override { return _block_size;  }
		Block::sector_t    block_count() override { return _block_count; }
		Block::Session::Operations ops() override { return _block_ops;   }

		void read(Block::sector_t           block_number,
		          Genode::size_t            block_count,
		          char                     *buffer,
		          Block::Packet_descriptor &packet) override
		{
			/* range check is done by Block::Driver */
			if (!_block_ops.supported(Block::Packet_descriptor::READ)) { throw Io_error(); }

			Vfs::file_size const len = block_count * _block_size;
			uint64_t const offset    = _lookup_disk_sector(block_number);
			if (offset == ~0u) {
				Genode::memset(buffer, 0, len);
			} else {
				/* XXX check if len spans multiple blocks */
				Vfs::file_size n = 0;
				_vdi_file->seek((Vfs::file_offset)offset);
				_vdi_file->fs().complete_read(_vdi_file, buffer, len, n);
				if (n != len) { throw Io_error(); }
			}

			ack_packet(packet);
		}

		void write(Block::sector_t           block_number,
		           Genode::size_t            block_count,
		           char const               *buffer,
		           Block::Packet_descriptor &packet) override
		{
			/* range check is done by Block::Driver */
			if (!_block_ops.supported(Block::Packet_descriptor::WRITE)) {
				throw Io_error();
			}

			Vfs::file_size const len = block_count * _block_size;
			uint64_t offset          = _lookup_disk_sector(block_number);
			if (offset == ~0u) {
				if (!_allocate_block(block_number)) { throw Io_error(); }

				offset = _lookup_disk_sector(block_number);
			}
			if (offset == ~0u) { throw Io_error(); }

			using Write_result = Vfs::File_io_service::Write_result;

			/* XXX check if len spans multiple blocks */
			Vfs::file_size n = 0;
			_vdi_file->seek((Vfs::file_offset)offset);
			Write_result res = _vdi_file->fs().write(_vdi_file, buffer, len, n);
			if (n != len || res != Vfs::File_io_service::WRITE_OK) { throw Io_error(); }

			ack_packet(packet);
		}

		void sync() { _vdi_file->fs().complete_sync(_vdi_file); }
};


struct Main
{
	Genode::Env  &_env;
	Genode::Heap  _heap { _env.ram(), _env.rm() };

	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	struct Factory : Block::Driver_factory
	{
		Genode::Constructible<Vdi_block_driver> _driver { };

		Factory(Genode::Env &env, Genode::Xml_node config)
		{
			_driver.construct(env, config);
		}

		~Factory() { _driver.destruct(); }

		/***********************
		 ** Factory interface **
		 ***********************/

		Block::Driver *create() { return &*_driver; }
		void destroy(Block::Driver *) { }
	} factory { _env, _config_rom.xml() };

	Block::Root root { _env.ep(), _heap, _env.rm(), factory,
	                   xml_attr_ok(_config_rom.xml(), "writeable") };

	Main(Genode::Env &env) : _env(env)
	{
		_env.parent().announce(_env.ep().manage(root));
	}
};


void Component::construct(Genode::Env &env)
{
	static Main main(env);
}
