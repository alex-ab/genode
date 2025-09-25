/*
 * \brief  I/O memory backend for ACPICA library and lookup code
 *         for initial ACPI RSDP pointer
 * \author Alexander Boettcher
 * \date   2016-11-14
 */

/*
 * Copyright (C) 2016-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_io_mem_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <base/sleep.h>
#include <util/misc_math.h>

#include <io_mem_session/connection.h>
#include <region_map/client.h>
#include <rm_session/connection.h>

#include "env.h"

extern "C" {
#include "acpi.h"
#include "acpiosxf.h"
#include "accommon.h"
#include "actables.h"
}

#define FAIL(retval) \
	{ \
		Genode::error(__func__, ":", __LINE__, " called - dead"); \
		while (1) Genode::sleep_forever(); \
		return retval; \
	}

namespace Acpica {
	class Io_mem;
	class Rsdp;
};


class Acpica::Rsdp
{
	private:

		/* BIOS range to scan for RSDP */
		enum { BIOS_BASE = 0xe0000, BIOS_SIZE = 0x20000 };

		/**
		 * Search for RSDP pointer signature in area
		 */
		Genode::uint8_t *_search_rsdp(Genode::uint8_t *area,
		                              Genode::size_t const area_size)
		{
			for (Genode::addr_t addr = 0; area && addr < area_size; addr += 16)
				/* XXX checksum table */
				if (!Genode::memcmp(area + addr, "RSD PTR ", 8))
					return area + addr;

			return nullptr;
		}

		/**
		 * Return 'Root System Descriptor Pointer' (ACPI spec 5.2.5.1)
		 */
		Genode::uint64_t _rsdp(Genode::Env &env)
		{
			Genode::uint8_t * local = 0;

			/* try BIOS area */
			{
				Genode::Attached_io_mem_dataspace io_mem(env, BIOS_BASE, BIOS_SIZE);
				local = _search_rsdp(io_mem.local_addr<Genode::uint8_t>(), BIOS_SIZE);
				if (local)
					return BIOS_BASE + (local - io_mem.local_addr<Genode::uint8_t>());
			}

			/* search EBDA (BIOS addr + 0x40e) */
			try {
				unsigned short base = 0;
				{
					Genode::Attached_io_mem_dataspace io_mem(env, 0, 0x1000);
					local = io_mem.local_addr<Genode::uint8_t>();
					if (local)
						base = (*reinterpret_cast<unsigned short *>(local + 0x40e)) << 4;
				}

				if (!base)
					return 0;

				Genode::Attached_io_mem_dataspace io_mem(env, base, 1024);
				local = _search_rsdp(io_mem.local_addr<Genode::uint8_t>(), 1024);

				if (local)
					return base + (local - io_mem.local_addr<Genode::uint8_t>());
			} catch (...) {
				Genode::warning("failed to scan EBDA for RSDP root");
			}

			return 0;
		}

	public:

		Rsdp() { }

		Genode::addr_t phys_rsdp(Genode::Env &env)
		{
			Genode::uint64_t phys_rsdp = _rsdp(env);
			return phys_rsdp;
		}
};


class Acpica::Io_mem
{
	private:

		typedef Genode::Capability<Platform::Device_interface> Platform_cap;

		ACPI_PHYSICAL_ADDRESS      _phys { };
		ACPI_SIZE                  _size { };
		Genode::uint8_t           *_virt { };
		unsigned                   _ref  { };

		Genode::Io_mem_connection           *_io_mem          { };
		Genode::Io_mem_dataspace_capability  _io_ds_cap       { };
		Platform_cap                         _platform_device { };

		static Genode::Rm_connection *rm_conn;

		static auto &_io_mems() { static Acpica::Io_mem io[32]; return io; }

	public:

		bool unused() const {
			return !_phys && !_size && !_io_mem && !_platform_device.valid(); }
		bool stale() const {
			return !unused() && !_io_mem && !_platform_device.valid(); }
		bool valid_iomem() const {
			return _io_mem || _platform_device.valid(); }

		bool contains_virt (const Genode::uint8_t * v, const ACPI_SIZE s) const
		{
			return _virt <= v && v + s <= _virt + _size;
		}
		bool contains_phys (const ACPI_PHYSICAL_ADDRESS p, const ACPI_SIZE s) const
		{
			return _phys <= p && p + s <= _phys + _size;
		}

		Genode::addr_t to_virt(ACPI_PHYSICAL_ADDRESS p) {
			_ref ++;
			return reinterpret_cast<Genode::addr_t>(_virt + (p - _phys));
		}

		void release()
		{
			if (_io_mem)
				Genode::destroy(Acpica::heap(), _io_mem);

			if (_platform_device.valid())
				Acpica::platform().release_device(_platform_device);
		}

		static void force_free_overlap(ACPI_PHYSICAL_ADDRESS const phys,
		                               ACPI_SIZE const size)
		{
			Acpica::Io_mem::apply_u([&] (Acpica::Io_mem &io_mem) {
				if (io_mem.unused() && !io_mem.stale())
					return 0;

				/* skip non overlapping ranges */
				if ((phys + size <= io_mem._phys) ||
				    (io_mem._phys + io_mem._size <= phys))
					return 0;

				while (io_mem._ref > 1) {
					io_mem.ref_dec();
				}

				Genode::warning(" force freeing I/O memory",
				                " unused=", io_mem.unused(),
				                " stale=" , io_mem.stale(),
				                " phys="  , Genode::Hex(io_mem._phys),
				                " size="  , Genode::Hex(io_mem._size),
				                " virt="  , io_mem._virt,
				                " io_ptr=", io_mem._io_mem,
				                " refcnt=", io_mem._ref);

				/* allocate region on heap and don't free, otherwise in destructor the connection will be closed */
				if (!rm_conn)
					rm_conn = new (Acpica::heap()) Genode::Rm_connection(Acpica::env());

				/* create managed dataspace to let virt region reserved */
				Genode::Region_map_client managed_region(rm_conn->create(io_mem._size));
				/* remember virt, since it get invalid during invalidate() */
				Genode::addr_t const orig_virt = reinterpret_cast<Genode::addr_t>(io_mem._virt);

				/* drop I/O mem and virt region get's freed */
				io_mem.invalidate();

				/* re-attach dummy managed dataspace to virt region */
				Genode::addr_t const re_attached_virt =
					Acpica::env().rm().attach(managed_region.dataspace(), {
						.size       = { },    .offset    = { },
						.use_at     = true,   .at        = orig_virt,
						.executable = false,  .writeable = true
					}).convert<Genode::addr_t>(
						[&] (Genode::Env::Local_rm::Attachment &a) {
							a.deallocate = false;
							return Genode::addr_t(a.ptr); },
						[&] (Genode::Env::Local_rm::Error) { return 0UL; }
					);

				if (orig_virt != re_attached_virt)
					FAIL(0);

				if (!io_mem.unused() || io_mem.stale())
					FAIL(0);

				return 0;
			});
		}

		bool ref_dec() { return --_ref; }

		static void apply_to_all(auto const &fn) {
			for (auto & e : _io_mems()) fn(e); }

		static Genode::addr_t apply_u(auto const &fn)
		{
			for (auto & e : _io_mems()) {
				auto const r = fn(e);
				if (r) return r;
			}

			return 0UL;
		}

		static auto with_unused_slot(auto const &fn, auto const &err)
		{
			for (auto & e : _io_mems()) {
				if (!e.unused())
					continue;

				return fn(e);
			}

			return err();
		}

		void invalidate()
		{
			if (unused())
				FAIL();

			if (stale()) {
				/**
				 * Look for the larger entry that replaced this one.
				 * Required to decrement ref count.
				 */
				apply_to_all([&] (Acpica::Io_mem &io_mem) {

					if (&io_mem == this)
						return;

					if (!io_mem.contains_phys(_phys, _size))
						return;

					if (io_mem.ref_dec())
						return;

					io_mem._ref++;
					io_mem.invalidate();
				});
			}

			if (ref_dec())
				return;

			if (!stale()) {
				Acpica::env().rm().detach(Genode::addr_t(_virt));
				release();
			}

			_phys = _size    = { };
			_virt            = { };
			_io_mem          = { };
			_io_ds_cap       = { };
			_platform_device = { };
		}

		static Acpica::Io_mem * allocate(ACPI_PHYSICAL_ADDRESS, ACPI_SIZE,
		                                 unsigned ref);

		static bool allocate_via_platform(Acpica::Io_mem &,
		                                  Platform::Device_interface::Range const &);

		static Genode::addr_t insert(ACPI_PHYSICAL_ADDRESS p, ACPI_SIZE s)
		{
			Acpica::Io_mem * io_mem = allocate(p, s, 1);
			if (!io_mem)
				return 0UL;

			io_mem->_virt = Acpica::env().rm().attach(io_mem->_io_ds_cap, {
				.size       = io_mem->_size,  .offset    = { },
				.use_at     = { },            .at        = { },
				.executable = { },            .writeable = true
			}).convert<Genode::uint8_t *>(
				[&] (Genode::Env::Local_rm::Attachment &a) {
					a.deallocate = false;
					return (Genode::uint8_t *)a.ptr; },
				[&] (Genode::Env::Local_rm::Error) { return nullptr; }
			);

			return reinterpret_cast<Genode::addr_t>(io_mem->_virt);
		}

		Genode::addr_t pre_expand(ACPI_PHYSICAL_ADDRESS p, ACPI_SIZE s)
		{
			if (valid_iomem()) {
				Acpica::env().rm().detach(Genode::addr_t(_virt));
				release();
			}

			Genode::addr_t xsize = _phys - p + _size;
			if (!allocate(p, xsize, _ref))
				FAIL(0)

			return _expand(p, s);
		}

		Genode::addr_t post_expand(ACPI_PHYSICAL_ADDRESS p, ACPI_SIZE s)
		{
			if (valid_iomem()) {
				Acpica::env().rm().detach(Genode::addr_t(_virt));
				release();
			}

			ACPI_SIZE xsize = p + s - _phys;
			if (!allocate(_phys, xsize, _ref))
				FAIL(0)

			return _expand(p, s);
		}

		Genode::addr_t _expand(ACPI_PHYSICAL_ADDRESS const p, ACPI_SIZE const s)
		{
			/* mark this element as a stale reference */
			_io_mem          = { };
			_platform_device = { };
			_io_ds_cap       = { };

			/* find new created entry */
			Genode::addr_t res = Acpica::Io_mem::apply_u([&] (Acpica::Io_mem &io_mem) {
				if (io_mem.unused() || io_mem.stale() ||
				    !io_mem.contains_phys(p, s))
					return 0UL;

				auto const io_ds = io_mem._io_ds_cap;

				/* re-attach mem of stale entries partially using this iomem */
				Acpica::Io_mem::apply_to_all([&] (Acpica::Io_mem &io2) {
					if (io2.unused() || !io2.stale() ||
					    !io_mem.contains_phys(io2._phys, 0))
						return;

					Genode::addr_t off_phys = io2._phys - io_mem._phys;

					Genode::addr_t virt = reinterpret_cast<Genode::addr_t>(io2._virt);

					Acpica::env().rm().detach(virt);

					Acpica::env().rm().attach(io_ds, {
						.size       = io2._size,  .offset    = off_phys,
						.use_at     = true,       .at        = virt,
						.executable = { },        .writeable = true
					}).with_result(
						[&] (auto &a) { a.deallocate = false; },
						[&] (auto) {
							Genode::error("re-attach io2 failed");
							FAIL();
						}
					);
				});

				if (io_mem._virt)
					FAIL(0UL);

				/* attach whole memory */
				io_mem._virt = Acpica::env().rm().attach(io_ds, {
					.size       = { },  .offset    = { },
					.use_at     = { },  .at        = { },
					.executable = { },  .writeable = true
				}).convert<Genode::uint8_t *>(
					[&] (Genode::Env::Local_rm::Attachment &a) {
						a.deallocate = false;
						return (Genode::uint8_t *)a.ptr; },
					[&] (Genode::Region_map::Attach_error) {
						Genode::error("attaching while io_ds failed");
						return nullptr;
					}
				);
				return io_mem.to_virt(p);
			});

			/* should never happen */
			if (!res)
				FAIL(0)

			return res;
		}
};


Acpica::Io_mem * Acpica::Io_mem::allocate(ACPI_PHYSICAL_ADDRESS p,
                                          ACPI_SIZE s, unsigned ref)
{
	using namespace Genode;

	return with_unused_slot([&](auto &io_mem) {

		ACPI_PHYSICAL_ADDRESS const phys = p & ~0xFFFUL;
		ACPI_SIZE             const size = align_addr(p + s - phys, 12);

		if (!allocate_via_platform(io_mem, { phys, size })) {
			auto conn = new (Acpica::heap()) Io_mem_connection(Acpica::env(), phys, size);

			if (!conn->dataspace().valid()) {
				destroy(Acpica::heap(), conn);
				error("io mem not available ", Hex_range(phys, size));

				return static_cast<Acpica::Io_mem *>(nullptr);
			}

			io_mem._io_mem    = conn;
			io_mem._io_ds_cap = conn->dataspace();
		}

		io_mem._phys = phys;
		io_mem._size = size;
		io_mem._ref  = ref;
		io_mem._virt = 0;

		return &io_mem;
	}, []() { return static_cast<Acpica::Io_mem *>(nullptr); });
}


Genode::Rm_connection * Acpica::Io_mem::rm_conn { };


bool Acpica::Io_mem::allocate_via_platform(Acpica::Io_mem &io_mem,
                                           Platform::Device_interface::Range const &phys_range)
{
	using namespace Genode;

	bool found = false;

	Rom_session_client rsc(Acpica::platform().devices_rom());
	auto rom_ds = rsc.dataspace();
	if (!rom_ds.valid())
		return false;

	Attached_dataspace rom(Acpica::env().rm(), rom_ds);
	if (!rom.size())
		return false;

	Node const devs(Const_byte_range_ptr(rom.local_addr<char>(), rom.size()));

	devs.for_each_sub_node("device", [&](auto const &dev) {
		if (found)
			return;

		dev.for_each_sub_node("io_mem", [&](auto const &io) {
			if (found)
				return;

			auto p = io.attribute_value("phys_addr", 0ull);
			auto s = io.attribute_value("size", 0ull);
			auto bar = io.attribute_value("pci_bar", 0ull);

			if (!p && !s)
				return;

			s += p & 0xffful;
			s  = align_addr(s, 12);
			p &= ~0xffful;

			if (!((p <= phys_range.start) && ((p + s) >= (phys_range.start + phys_range.size))))
				return;

			auto name = dev.attribute_value("name", Platform::Session::Device_name());

			Platform::Device_interface::Range range { };

			io_mem._platform_device = Acpica::platform().acquire_device(name.string());
			auto io_mem_cap = io_mem._platform_device.call<Platform::Device_interface::Rpc_io_mem>( bar, range);

			io_mem._io_mem    = { };
			io_mem._io_ds_cap = Io_mem_session_client(io_mem_cap).dataspace();

			found = true;
		});
	});

	return found;
}


static ACPI_TABLE_RSDP faked_rsdp;
enum { FAKED_PHYS_RSDP_ADDR = 1 };

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer (void)
{
	Genode::Env &env = Acpica::env();

	/* try platform_info ROM provided by core */
	Genode::Attached_rom_dataspace info(env, "platform_info");

	using Genode::memcpy;

	ACPI_MAKE_RSDP_SIG(faked_rsdp.Signature);
	memcpy(faked_rsdp.OemId, "Faked", 6);
	faked_rsdp.Checksum = 0;
	info.node().with_optional_sub_node("acpi", [&] (Genode::Node const &acpi_node) {
		faked_rsdp.Revision            = acpi_node.attribute_value("revision", 0U);
		faked_rsdp.RsdtPhysicalAddress = acpi_node.attribute_value("rsdt", 0UL);
		faked_rsdp.Length              = sizeof(ACPI_TABLE_RSDP);
		faked_rsdp.XsdtPhysicalAddress = acpi_node.attribute_value("xsdt", 0UL);

		/* update checksum */
		faked_rsdp.Checksum = (UINT8) -AcpiTbChecksum((UINT8 *)&faked_rsdp,
		                                              ACPI_RSDP_CHECKSUM_LENGTH);
	});

	if (faked_rsdp.XsdtPhysicalAddress || faked_rsdp.RsdtPhysicalAddress)
		return FAKED_PHYS_RSDP_ADDR;

	/* legacy way - search and grep for the pointer */
	Acpica::Rsdp acpi_table;
	ACPI_PHYSICAL_ADDRESS rsdp = acpi_table.phys_rsdp(env);
	return rsdp;
}

void * AcpiOsMapMemory (ACPI_PHYSICAL_ADDRESS phys, ACPI_SIZE size)
{
	if (phys == FAKED_PHYS_RSDP_ADDR)
		return &faked_rsdp;

	Genode::addr_t virt = Acpica::Io_mem::apply_u([&] (Acpica::Io_mem &io_mem) {
		if (io_mem.unused() || io_mem.stale())
			return 0UL;

		if (io_mem.contains_phys(phys, size))
			/* we have already a mapping in which the request fits */
			return io_mem.to_virt(phys);

		if (io_mem.contains_phys(phys + 1, 0))
			/* phys is within region but end is outside region */
			return io_mem.post_expand(phys, size);

		if (io_mem.contains_phys(phys + size - 1, 0))
			/* phys starts before region and end is within region */
			return io_mem.pre_expand(phys, size);

		return 0UL;
	});

	if (virt)
		return reinterpret_cast<void *>(virt);

	virt= Acpica::Io_mem::insert(phys, size);
	if (virt)
		return reinterpret_cast<void *>(virt + (phys & 0xfffU));

	return 0UL;
}

void AcpiOsUnmapMemory (void * ptr, ACPI_SIZE size)
{
	if (ptr == &faked_rsdp)
		return;

	Genode::uint8_t const * virt = reinterpret_cast<Genode::uint8_t *>(ptr);

	if (Acpica::Io_mem::apply_u([&] (Acpica::Io_mem &io_mem) {
		if (io_mem.unused() || !io_mem.contains_virt(virt, size))
			return 0;

		io_mem.invalidate();
		return 1;
	}))
		return;

	FAIL()
}

void AcpiGenodeFreeIOMem(ACPI_PHYSICAL_ADDRESS const phys, ACPI_SIZE const size)
{
	Acpica::Io_mem::force_free_overlap(phys, size);
}
