/*
 * \brief  Multiboot 2 handling
 * \author Alexander Boettcher
 * \date   2017-08-11
 */

/*
 * Copyright (C) 2017-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* base includes */
#include <util/mmio.h>

namespace Genode { class Multiboot2_info; }

class Genode::Multiboot2_info : Mmio<0x8>
{
	private:

		struct Size : Register <0x0, 32> { };

		template <size_t SIZE>
		struct Tag_tpl : Genode::Mmio<SIZE>
		{
			enum { LOG2_SIZE = 3 };

			struct Type : Register <0x00, 32>
			{
				enum {
					END                 = 0,
					BOOT_CMDLINE        = 1,
					MODULE              = 3,
					MEMORY              = 6,
					FRAMEBUFFER         = 8,
					EFI_SYSTEM_TABLE_64 = 12,
					ACPI_RSDP_V1        = 14,
					ACPI_RSDP_V2        = 15,
					EFI_IMAGE_PTR_32    = 19,
					EFI_IMAGE_PTR_64    = 20,
				};
			};
			struct Size : Register <0x04, 32> { };

			Tag_tpl(addr_t addr) : Mmio<SIZE>({(char *)addr, SIZE}) { }
		};

		using Tag = Tag_tpl<0x8>;

		struct Efi_system_table_64 : Tag_tpl<0x10>
		{
			struct Pointer : Register <0x08, 64> { };

			Efi_system_table_64(addr_t addr) : Tag_tpl(addr) { }
		};

	public:

		enum { MAGIC = 0x36d76289UL };

		struct Memory : Genode::Mmio<0x14>
		{
			enum { SIZE = 3 * 8 };

			struct Addr : Register <0x00, 64> { };
			struct Size : Register <0x08, 64> { };
			struct Type : Register <0x10, 32> {
				enum {
					AVAILABLE_MEMORY    = 1,
					RESERVED_MEMORY     = 2,
					ACPI_RECLAIM_MEMORY = 3,
					ACPI_NVS_MEMORY     = 4
				 };
			};

			Memory(addr_t mmap) : Mmio({(char *)mmap, Mmio::SIZE}) { }
		};

		struct Framebuffer : Genode::Mmio<22>
		{
			struct Addr   : Register <0x00, 64> { };
			struct Pitch  : Register <0x08, 32> { };
			struct Width  : Register <0x0c, 32> { };
			struct Height : Register <0x10, 32> { };
			struct Bpp    : Register <0x14,  8> { };
			struct Type   : Register <0x15,  8> { };

			Framebuffer(addr_t mmap) : Mmio({(char *)mmap, 22}) { }
		};

		struct Acpi_rsdp
		{
			Genode::uint64_t signature { 0 };
			Genode::uint8_t  checksum  { 0 };
			char             oem[6]    { 0 };
			Genode::uint8_t  revision  { 0 };
			Genode::uint32_t rsdt      { 0 };
			Genode::uint32_t length    { 0 };
			Genode::uint64_t xsdt      { 0 };
			Genode::uint32_t reserved  { 0 };

			bool valid()
			{
				const char sign[] = "RSD PTR ";
				return signature == *(Genode::uint64_t *)sign;
			}

		} __attribute__((packed));

		Multiboot2_info(addr_t mbi) : Mmio({(char *)mbi, Mmio::SIZE}) { }

		addr_t size() const { return read<Multiboot2_info::Size>(); }

		void for_each_tag(auto const &mem_fn,
		                  auto const &acpi_rsdp_v1_fn,
		                  auto const &acpi_rsdp_v2_fn,
		                  auto const &fb_fn,
		                  auto const &systab64_fn,
		                  auto const &cmd_fn,
		                  auto const &module_fn,
		                  auto const &fb_image)
		{
			addr_t const size = read<Multiboot2_info::Size>();

			for (addr_t tag_addr = base() + (1UL << Tag::LOG2_SIZE);
			     tag_addr < base() + size;)
			{
				Tag tag(tag_addr);

				if (tag.read<Tag::Type>() == Tag::Type::END)
					return;

				if (tag.read<Tag::Type>() != Tag::Type::EFI_SYSTEM_TABLE_64 &&
				    tag.read<Tag::Type>() != Tag::Type::MEMORY              &&
				    tag.read<Tag::Type>() != Tag::Type::ACPI_RSDP_V1        &&
				    tag.read<Tag::Type>() != Tag::Type::ACPI_RSDP_V2        && 
				    tag.read<Tag::Type>() != Tag::Type::BOOT_CMDLINE        && 
				    tag.read<Tag::Type>() != Tag::Type::MODULE              &&
				    tag.read<Tag::Type>() != Tag::Type::EFI_IMAGE_PTR_32    &&
				    tag.read<Tag::Type>() != Tag::Type::EFI_IMAGE_PTR_64    &&
				    tag.read<Tag::Type>() != Tag::Type::FRAMEBUFFER)
					warning ("mbi2 : unhandled type=", tag.read<Tag::Type>());

				if (tag.read<Tag::Type>() == Tag::Type::BOOT_CMDLINE) {
					addr_t cmd_start = tag_addr + (1UL << Tag::LOG2_SIZE);
					cmd_fn(cmd_start, tag.read<Tag::Size>() - (1UL << Tag::LOG2_SIZE));
				}

				if (tag.read<Tag::Type>() == Tag::Type::MODULE) {
					addr_t mod_start = tag_addr + (1UL << Tag::LOG2_SIZE);
					addr_t mod_end   = tag_addr + (1UL << Tag::LOG2_SIZE) + 4;
					addr_t mod_cmd   = tag_addr + (1UL << Tag::LOG2_SIZE) + 8;

					mod_start = *(unsigned *)mod_start; 
					mod_end   = *(unsigned *)mod_end; 

					module_fn(mod_start, mod_end, mod_cmd,
					          tag.read<Tag::Size>() - (1UL << Tag::LOG2_SIZE) - 8);
				}

				if (tag.read<Tag::Type>() == Tag::Type::EFI_SYSTEM_TABLE_64) {
					Efi_system_table_64 const est(tag_addr);
					systab64_fn(est.read<Efi_system_table_64::Pointer>());
				}

				if (tag.read<Tag::Type>() == Tag::Type::MEMORY) {
					addr_t mem_start = tag_addr + (1UL << Tag::LOG2_SIZE) + 8;
					addr_t const mem_end = tag_addr + tag.read<Tag::Size>();

					for (; mem_start < mem_end; mem_start += Memory::SIZE) {
						Memory mem(mem_start);
						mem_fn(mem);
					}
				}

				if (tag.read<Tag::Type>() == Tag::Type::ACPI_RSDP_V1 ||
				    tag.read<Tag::Type>() == Tag::Type::ACPI_RSDP_V2) {

					size_t const sizeof_tag = 1UL << Tag::LOG2_SIZE;
					addr_t const rsdp_addr  = tag_addr + sizeof_tag;

					Acpi_rsdp * rsdp = reinterpret_cast<Acpi_rsdp *>(rsdp_addr);

					if (tag.read<Tag::Size>() - sizeof_tag == 20) {
						/* XXX ACPI RSDP v1 is 20 byte solely */
						acpi_rsdp_v1_fn(*rsdp);
					} else {
						/* ACPI RSDP v2 */
						acpi_rsdp_v2_fn(*rsdp);
					}
				}

				if (tag.read<Tag::Type>() == Tag::Type::FRAMEBUFFER) {
					size_t const sizeof_tag = 1UL << Tag::LOG2_SIZE;

					Framebuffer const fb(tag_addr + sizeof_tag);

					fb_fn(fb);
				}

				if (tag.read<Tag::Type>() == Tag::Type::EFI_IMAGE_PTR_32) {
					addr_t tag_info = tag_addr + (1UL << Tag::LOG2_SIZE);
					auto image_ptr = *reinterpret_cast<unsigned *>(tag_info);
					fb_image(image_ptr);
				}

				if (tag.read<Tag::Type>() == Tag::Type::EFI_IMAGE_PTR_64) {
					addr_t tag_info = tag_addr + (1UL << Tag::LOG2_SIZE);
					auto image_ptr = *reinterpret_cast<unsigned long *>(tag_info);
					fb_image(image_ptr);
				}

				tag_addr += align_addr(tag.read<Tag::Size>(), Tag::LOG2_SIZE);
			}
		}
};
