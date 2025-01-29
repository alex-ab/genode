/*
 * \brief  Platform interface implementation
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \author Alexander Boettcher
 * \date   2009-10-02
 */

/*
 * Copyright (C) 2009-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/sleep.h>
#include <base/thread.h>
#include <util/bit_array.h>
#include <util/mmio.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <trace/source_registry.h>
#include <util/construct_at.h>

/* core includes */
#include <boot_modules.h>
#include <core_log.h>
#include <platform.h>
#include <novae_util.h>
#include <util.h>
#include <ipc_pager.h>
#include <multiboot2.h>

/* base-internal includes */
#include <base/internal/stack_area.h>
#include <base/internal/native_utcb.h>
#include <base/internal/globals.h>

/* NOVAe includes */
#include <novae/syscalls.h>
#include <novae/util.h>

using namespace Core;
using namespace Novae;


enum { verbose_boot_info = true };



/**
 * Initial value of esp register, saved by the crt0 startup code.
 * This value contains the address of the hypervisor information page.
 */
extern addr_t __initial_sp;
extern addr_t __initial_di;
extern addr_t __initial_si;


/**
 * Pointer to the UTCB of the main thread
 */
static Utcb *__main_thread_utcb;


/**
 * Virtual address range consumed by core's program image
 */
extern unsigned _prog_img_beg, _prog_img_end;


/**
 * Map preserved physical pages core-exclusive
 *
 * This function uses the virtual-memory region allocator to find a region
 * fitting the desired mapping. Other allocators are left alone.
 */
addr_t Core::Platform::_map_pages(addr_t const phys_addr, addr_t const pages,
                                  bool guard_page)
{
	addr_t const size = pages << get_page_size_log2();

	/* try to reserve contiguous virtual area */
	return region_alloc().alloc_aligned(size + (guard_page ? get_page_size() : 0),
	                                    get_page_size_log2()).convert<addr_t>(
		[&] (void *core_local_ptr) {

			addr_t const core_local_addr = reinterpret_cast<addr_t>(core_local_ptr);

			auto res = map_local(Platform::kernel_host_sel(),
			                     Platform::core_host_sel(),
			                     phys_addr, core_local_addr, pages,
			                     Rights::rw());

			return res != Novae::NOVA_OK ? 0 : core_local_addr;
		},

		[&] (Allocator::Alloc_error) {
			return 0UL; });
}


/*****************************
 ** Core page-fault handler **
 *****************************/


enum { CORE_PAGER_UTCB_ADDR = 0xbff02000 };

Native_utcb *main_thread_utcb();

/**
 * IDC handler for the page-fault portal
 */
static void page_fault_handler()
{
	Utcb *utcb = (Utcb *)CORE_PAGER_UTCB_ADDR;

	addr_t  const pf_addr = utcb->pf_addr();
	addr_t  const pf_ip   = utcb->ip();
	addr_t  const pf_sp   = utcb->sp();
	uint8_t const pf_type = utcb->pf_type();

	error("PAGE-FAULT IN CORE addr=", Hex(pf_addr), " ip=", Hex(pf_ip),
	      " (", (pf_type & Ipc_pager::ERR_W) ? "write" : "read", ")");

	error(" main thread utcb ", main_thread_utcb());

	log("\nstack pointer ", Hex(pf_sp), ", qualifiers ", Hex(pf_type), " ",
	    pf_type & Ipc_pager::ERR_I ? "I" : "i",
	    pf_type & Ipc_pager::ERR_R ? "R" : "r",
	    pf_type & Ipc_pager::ERR_U ? "U" : "u",
	    pf_type & Ipc_pager::ERR_W ? "W" : "w",
	    pf_type & Ipc_pager::ERR_P ? "P" : "p");

	/* dump stack trace */
	struct Core_img
	{
		addr_t  _beg = 0;
		addr_t  _end = 0;
		addr_t *_ip  = nullptr;

		Core_img(addr_t sp)
		{
			extern addr_t _dtors_end;
			_beg = (addr_t)&_prog_img_beg;
			_end = (addr_t)&_dtors_end;

			_ip = (addr_t *)sp;
			for (;!ip_valid(); _ip++) {}
		}

		addr_t *ip()       { return _ip; }
		void    next_ip()  { _ip = ((addr_t *)*(_ip - 1)) + 1;}
		bool    ip_valid() { return (*_ip >= _beg) && (*_ip < _end); }
	};

	int count = 1;
	log("  #", count++, " ", Hex(pf_sp, Hex::PREFIX, Hex::PAD), " ",
	                         Hex(pf_ip, Hex::PREFIX, Hex::PAD));

	Core_img dump(pf_sp);
	while (dump.ip_valid()) {
		log("  #", count++, " ", Hex((addr_t)dump.ip(), Hex::PREFIX, Hex::PAD),
		                    " ", Hex(*dump.ip(),        Hex::PREFIX, Hex::PAD));
		dump.next_ip();
	}

	sleep_forever();
}


static addr_t core_pager_stack_top()
{
	enum { STACK_SIZE = 4*1024 };
	static char stack[STACK_SIZE];
	return (addr_t)&stack[STACK_SIZE - sizeof(addr_t)];
}


/**
 * Startup handler for core IRQ threads
 */
__attribute__((regparm(1)))
static void startup_handler(Genode::addr_t const utcb_new_global_ec)
{
	Utcb & utcb     = *reinterpret_cast<Utcb *>(CORE_PAGER_UTCB_ADDR);
	Utcb & utcb_new = *reinterpret_cast<Utcb *>(utcb_new_global_ec);

	utcb.ip(utcb_new.ip());
	utcb.sp(utcb_new.sp());

	reply(reinterpret_cast<void *>(core_pager_stack_top()),
	      Mtd::EIP | Mtd::ESP);
}

static void init_core_page_fault_handler(auto const core_cap_sel,
                                         auto const core_pd_sel,
                                         auto const boot_cpu)
{
	/* create fault handler EC for core main thread */
	enum { EXC_BASE = 0 };

	addr_t ec_sel = cap_map().insert(1);

	uint8_t ret = create_ec(ec_sel, core_pd_sel, boot_cpu,
	                        CORE_PAGER_UTCB_ADDR,
	                        core_pager_stack_top(),
	                        EXC_BASE, false /* local */);
	if (ret != Novae::NOVA_OK)
		error(__func__, ":", __LINE__, " returned ", ret);

	/* set up page-fault portal */
	ret = create_pt(PT_SEL_PAGE_FAULT, core_pd_sel, ec_sel,
	                (addr_t)page_fault_handler);

	if (ret != Novae::NOVA_OK)
		error(__func__, ":", __LINE__, " returned ", ret);

	/* specify information received during page-fault */
	ret = pt_ctrl(PT_SEL_PAGE_FAULT, PT_SEL_PAGE_FAULT,
	              Mtd(Mtd::QUAL | Mtd::ESP | Mtd::EIP).value());
	if (ret != Novae::NOVA_OK)
		error(__func__, ":", __LINE__, " returned ", ret);

	/* specify portal rights -> remove RIGHT_PT_CTRL */
	ret = modify(core_cap_sel, Obj_crd(PT_SEL_PAGE_FAULT, 0,
	                                   Obj_crd::RIGHT_PT_EVENT |
	                                   Obj_crd::RIGHT_PT_CALL));
	if (ret != Novae::NOVA_OK)
		error(__func__, ":", __LINE__, " returned ", ret);

	/*
	 * startup portal for global core threads
	 * - used solely by irq_session_component to create IRQ threads
	 */
	ret = create_pt(PT_SEL_STARTUP, core_pd_sel, ec_sel,
	                (addr_t)startup_handler);
	if (ret != Novae::NOVA_OK)
		error(__func__, ":", __LINE__, " returned ", ret);
}


static bool cpuid_invariant_tsc()
{
	unsigned long cpuid = 0x80000007, edx = 0;
#ifdef __x86_64__
	asm volatile ("cpuid" : "+a" (cpuid), "=d" (edx) : : "rbx", "rcx");
#else
	asm volatile ("push %%ebx  \n"
	              "cpuid       \n"
	              "pop  %%ebx" : "+a" (cpuid), "=d" (edx) : : "ecx");
#endif
	return edx & 0x100;
}


static Affinity::Space setup_affinity_space(Hip const &hip)
{
	/* XXX hyperthreading detection missing */
	return Affinity::Space(hip.cpu_num, 1);
}


static void setup_io_port_access(Hip const & hip)
{
	async_map(        hip.sel_num - 1,      /* kernel object space */
	                  hip.sel_num - 2,      /* root   object space */
	          Obj_crd(hip.sel_num - 4, 0),  /* source - kernel PIO cap */
	          Obj_crd(hip.sel_num - 6, 0)); /* target selector in this space */

	async_map(        hip.sel_num - 1,      /* kernel object space */
	                  hip.sel_num - 2,      /* root   object space */
	          Obj_crd(hip.sel_num - 8, 0),  /* source - root PIO cap */
	          Obj_crd(hip.sel_num - 7, 0)); /* target selector in this space */

	async_map(hip.sel_num - 6, /* cap of kernel pio space */
	          hip.sel_num - 7, /* cap of root   pio space */
	          Io_crd(0, 16),   /* grant all IO ports from kernel PIO space */
	          Io_crd(0, 16));  /* take  all IO ports in   root   PIO space */
}


static void take_kernel_core_cap(Hip const & hip)
{
	async_map(        hip.sel_num - 1,      /* kernel object space */
	                  hip.sel_num - 2,      /* root   object space */
	          Obj_crd(hip.sel_num - 3, 0),  /* source - kernel host space cap */
	          Obj_crd(hip.sel_num - 8, 0)); /* target selector in this space */

	/* 'hip.sel_num - 8' is now Platform::kernel_host_sel() */

	async_map(        hip.sel_num - 1,      /* kernel object space */
	                  hip.sel_num - 2,      /* root   object space */
	          Obj_crd(hip.sel_num - 7, 0),  /* source - root host space cap */
	          Obj_crd(hip.sel_num - 9, 0)); /* target selector in this space */

	/* 'hip.sel_num - 9' is now Platform::core_host_sel() */
}


static void setup_bda_access(Mem_crd const dst)
{
	async_map(Core::Platform::kernel_host_sel(),
	          Core::Platform::core_host_sel(),
	          Mem_crd(0, 0),   /* grant first page with BDA content */
	          dst);            /* take        page and add to dst address */
}


struct Boot_info {
	struct {
		uint64_t addr;
		uint32_t width;
		uint32_t height;
		uint32_t pitch;
		uint8_t  type;
		uint8_t  bpp;
	} fb;

	uint64_t rsdt;
	uint64_t xsdt;
	uint64_t efi_sys_tab_phy;
	bool     efi_boot;
};


static bool setup_allocators(auto &mem_io, auto &mem_ram, auto &region_alloc,
                             auto &core_phys_start, Boot_info &boot_info)
{
	auto const phys_mbi = __initial_si;
	auto const offset   = phys_mbi & 0xfffu;
	void * map = nullptr;

	if (__initial_di != Multiboot2_info::MAGIC)
		return true; /* Multiboot v1 or other kinds of boot are unsupported */

	region_alloc.alloc_aligned(0x2000, 13).with_result([&] (void *ptr) {
		map = ptr;
	}, [&] (Allocator::Alloc_error) { /* !map is checked below */ });

	if (!map)
		return true;

	Mem_crd src_tmp1(    phys_mbi >> 12     , 0, Rights::read_only());
	Mem_crd dst_tmp1( addr_t(map) >> 12     , 0, Rights::read_only());
	Mem_crd src_tmp2((   phys_mbi >> 12) + 1, 0, Rights::read_only());
	Mem_crd dst_tmp2((addr_t(map) >> 12) + 1, 0, Rights::read_only());

	async_map(Core::Platform::kernel_host_sel(),
	          Core::Platform::core_host_sel(), src_tmp1, dst_tmp1);
	async_map(Core::Platform::kernel_host_sel(),
	          Core::Platform::core_host_sel(), src_tmp2, dst_tmp2);

	Multiboot2_info mbi2_tmp(dst_tmp1.addr() + offset);

	auto const map_size = mbi2_tmp.size() + offset;

	revoke(Core::Platform::core_host_sel(), dst_tmp1);
	revoke(Core::Platform::core_host_sel(), dst_tmp2);
	region_alloc.free(map);

	region_alloc.alloc_aligned(map_size, 12).with_result([&] (void *ptr) {
		map = ptr;
	}, [&] (Allocator::Alloc_error) { /* !map is checked below */ });

	if (!map)
		return true;

	Mem_crd src(   phys_mbi >> 12, 0, Rights::read_only());
	Mem_crd dst(addr_t(map) >> 12, 0, Rights::read_only());

	async_map(Core::Platform::kernel_host_sel(),
	          Core::Platform::core_host_sel(), src, dst);

	Multiboot2_info mbi2(dst.addr() + offset);

	auto phys_end  = phys_mbi + mbi2.size();
	auto phys_next = offset ? align_addr(phys_mbi, 12) : phys_mbi + 0x1000;
	auto virt_next = addr_t(map) + 4096;

	for (auto i = 0ul; phys_next + i < phys_end && i < map_size; i += 4096) {
		Mem_crd src_tmp((phys_next + i) >> 12, 0, Rights::read_only());
		Mem_crd dst_tmp((virt_next + i) >> 12, 0, Rights::read_only());

		error("extra map ", Hex(src_tmp.value()), "->", Hex(dst_tmp.value()));

		async_map(Core::Platform::kernel_host_sel(),
		          Core::Platform::core_host_sel(), src_tmp, dst_tmp);
	}

	/* remove MBI from RAM allocator */
	mem_ram.remove_range(phys_mbi & ~0xffful,
	                     align_addr(phys_end - phys_mbi + offset, 12));

	if (phys_end - phys_mbi >= map_size)
		warning(__func__, " not all MBI memory accessible");

	mbi2.for_each_tag([&] (Multiboot2_info::Memory const & m) {
		uint32_t const type = m.read<Multiboot2_info::Memory::Type>();

		if (type != Multiboot2_info::Memory::Type::AVAILABLE_MEMORY)
			return;

		uint64_t const base = m.read<Multiboot2_info::Memory::Addr>();
		uint64_t const size = m.read<Multiboot2_info::Memory::Size>();

		auto const offset       = base & 0xffful;
		auto const aligned_base = align_addr(base, 12);

		if (!size || (offset && (0x1000 - offset <= size)))
			return;

		auto const aligned_size = align_addr(size - (offset ? (0x1000 - offset) : 0), 12);

		mem_io .remove_range(aligned_base, aligned_size);
		mem_ram.add_range   (aligned_base, aligned_size);

		error("ram ", Hex(aligned_base), "+", Hex(aligned_size));
	},
	[&] (auto const /* rsdp_v1 */) { },
	[&] (auto const /* rsdp_v2 */) { },
	[&] (auto const &/* fb */) { },
	[&] (auto const /* efi_sys_tab*/) { },
	[&] (auto, auto /* cmd line */) { },
	[&] (auto, auto, auto, auto /* module */) { },
	[&] (auto const /* efi image ptr */) { });

	mbi2.for_each_tag([&] (Multiboot2_info::Memory const & m) {
		uint32_t const type = m.read<Multiboot2_info::Memory::Type>();

		if (type == Multiboot2_info::Memory::Type::AVAILABLE_MEMORY)
			return;

		uint64_t const base = m.read<Multiboot2_info::Memory::Addr>();
		uint64_t const size = m.read<Multiboot2_info::Memory::Size>();

		if (!size)
			return;

		auto const offset       = base &  0xffful;
		auto const aligned_base = base & ~0xffful;
		auto const aligned_size = align_addr(size + offset, 12);

		/* make acpi regions as io_mem available to platform driver */
		if (type == Multiboot2_info::Memory::Type::ACPI_RECLAIM_MEMORY ||
		    type == Multiboot2_info::Memory::Type::ACPI_NVS_MEMORY)
			mem_io.add_range(aligned_base, aligned_size);

		mem_ram.remove_range(aligned_base, aligned_size);

		error("ram ", Hex(aligned_base), "+", Hex(aligned_size), " remove");
	},
	[&] (auto const rsdp_v1) {
		if (!boot_info.rsdt)
			boot_info.rsdt = rsdp_v1.rsdt;
	},
	[&] (auto const rsdp_v2) {
		boot_info.xsdt = rsdp_v2.xsdt;
		boot_info.rsdt = rsdp_v2.rsdt;
	},
	[&] (Multiboot2_info::Framebuffer const &fb) {

		auto const phys_addr = fb.read<Multiboot2_info::Framebuffer::Addr>();
		auto const phys_size = fb.read<Multiboot2_info::Framebuffer::Pitch>()
		                     * fb.read<Multiboot2_info::Framebuffer::Height>();

		auto const offset = phys_addr & 0xffful;
		
		mem_ram.remove_range(phys_addr & ~0xffful,
		                     align_addr(phys_size + offset, 12));

		boot_info.fb.addr   = fb.read<Multiboot2_info::Framebuffer::Addr>();
		boot_info.fb.pitch  = fb.read<Multiboot2_info::Framebuffer::Pitch>();
		boot_info.fb.width  = fb.read<Multiboot2_info::Framebuffer::Width>();
		boot_info.fb.height = fb.read<Multiboot2_info::Framebuffer::Height>();
		boot_info.fb.bpp    = fb.read<Multiboot2_info::Framebuffer::Bpp>();
		boot_info.fb.type   = fb.read<Multiboot2_info::Framebuffer::Type>();
	},
	[&] (auto const efi_sys_tab) {
		boot_info.efi_sys_tab_phy = efi_sys_tab;
		boot_info.efi_boot = true;
	},
	[&] (auto cmdline, auto size) {
		auto const phys_cmd    = src.addr() + cmdline - dst.addr();
		auto const phys_offset = phys_cmd & 0xffful;

		mem_ram.remove_range(phys_cmd & ~0xffful,
		                     align_addr(size + phys_offset, 12));
	},
	[&](auto mod_start, auto mod_end, auto /* cmd */, auto /* cmd_size */) {

		if (mod_end <= mod_start)
			return;

		auto const mod_offset = mod_start & 0xffful;
		auto const mod_size   = mod_end - mod_start + mod_offset;

		mem_ram.remove_range(mod_start & ~0xffful, align_addr(mod_size, 12));

		if (!core_phys_start) {
			/* assume core's ELF image has one-page header */
			core_phys_start = mod_start + 0x1000;
		}
	},
	[&] (auto const efi_image_ptr) {
		if (efi_image_ptr)
			boot_info.efi_boot = true;
	});

	for (unsigned i = 0; i < map_size; i += 4096) {
		Mem_crd dst_revoke((dst.addr() >> 12) + i, 0, Rights::none());
		revoke(Core::Platform::core_host_sel(), dst_revoke);
	}
	region_alloc.free(map);

	return false;
}

/**************
 ** Platform **
 **************/

Core::Platform::Platform()
:
	_io_mem_alloc(&core_mem_alloc()), _io_port_alloc(&core_mem_alloc()),
	_irq_alloc(&core_mem_alloc()),
	_vm_base(0x1000), _vm_size(0), _cpus(Affinity::Space(1,1))
{
	bool warn_reorder  = false;
	bool error_memory  = true;
	bool error_overlap = false;
	bool error_remap   = false;

	Hip const &hip = *(Hip *)__initial_sp;

	/* determine number of available CPUs */
	_cpus = setup_affinity_space(hip);

	/* register UTCB of main thread */
	__main_thread_utcb = (Utcb *)(__initial_sp - get_page_size());

	/* create lock used by capability allocator */
	Novae::create_sm(Novae::SM_SEL_EC, core_pd_sel(), 0);

	/* locally map the whole I/O port range */
	setup_io_port_access(hip);

	/* get access to caps to manipulate later on host address space */
	take_kernel_core_cap(hip);

	/* map BDA region, console reads IO ports at BDA_VIRT_ADDR + 0x400 */
	enum { BDA_VIRT = 0x1U, BDA_VIRT_ADDR = BDA_VIRT << 12 };
	setup_bda_access(Mem_crd(BDA_VIRT, 0, Rights::read_only()));

	/*
	 * Now that we can access the I/O ports for comport 0, output works...
	 */

	/*
	 * Mark successful boot of hypervisor for automatic tests. This must be
	 * done before core_log is initialized to prevent unexpected-reboot
	 * detection.
	 */
	log("\nHypervisor '", Cstring((char *)&hip.signature, 4), "'e",
	    " cpus=", _cpus.width(), "x", _cpus.height());

	_cpus = Affinity::Space(1, 1);

	log("restrict to",
	    " cpus=", _cpus.width(), "x", _cpus.height());

#if 0
	/* init genode cpu ids based on kernel cpu ids (used for syscalls) */
	warn_reorder = !hip.remap_cpu_ids(map_cpu_ids,
	                                  sizeof(map_cpu_ids) / sizeof(map_cpu_ids[0]),
	                                  (unsigned)boot_cpu());
#endif

	/* configure virtual address spaces */
#ifdef __x86_64__
	_vm_size = 0x7fffc0000000UL - _vm_base;
#else
	error("not supported");
	Genode::sleep_forever();
#endif

	/* set up page fault handler for core - for debugging */
	init_core_page_fault_handler(core_obj_sel(), core_pd_sel(), hip.cpu_bsp);

	/* remap main UTCB to default utcb address */
	error_remap = map_local(Platform::core_host_sel(),
	                        Platform::core_host_sel(),
	                        addr_t(__main_thread_utcb),
	                        addr_t(main_thread_utcb()),
	                        1, Rights::rw()) != NOVA_OK;


	/* define core's virtual address space */
	addr_t virt_beg = _vm_base;
	addr_t virt_end = _vm_size;
	_core_mem_alloc.virt_alloc().add_range(virt_beg, virt_end - virt_beg);

	/* exclude core image from core's virtual address allocator */
	addr_t const core_virt_beg = trunc_page((addr_t)&_prog_img_beg);
	addr_t const core_virt_end = round_page((addr_t)&_prog_img_end);
	addr_t const binaries_beg  = trunc_page((addr_t)&_boot_modules_binaries_begin);
	addr_t const binaries_end  = round_page((addr_t)&_boot_modules_binaries_end);

	size_t const core_size     = binaries_beg - core_virt_beg;
	region_alloc().remove_range(core_virt_beg, core_size);

	/* ROM modules are un-used by core - de-detach region */
	addr_t const binaries_size  = binaries_end - binaries_beg;
	unmap_local(Platform::core_host_sel(), binaries_beg, binaries_size >> 12, { });

	/* preserve Bios Data Area (BDA) in core's virtual address space */
	region_alloc().remove_range(BDA_VIRT_ADDR, 0x1000);

	/* preserve stack area in core's virtual address space */
	region_alloc().remove_range(stack_area_virtual_base(),
	                            stack_area_virtual_size());

	/* exclude utcb of core pager thread + empty guard pages before and after */
	region_alloc().remove_range(CORE_PAGER_UTCB_ADDR - get_page_size(),
	                            get_page_size() * 3);

	/* exclude utcb of main thread and hip + empty guard pages before and after */
	region_alloc().remove_range((addr_t)__main_thread_utcb - get_page_size(),
	                            get_page_size() * 4);

	/* sanity checks */
	addr_t check [] = {
		reinterpret_cast<addr_t>(__main_thread_utcb), CORE_PAGER_UTCB_ADDR,
		BDA_VIRT_ADDR
	};

	for (unsigned i = 0; i < sizeof(check) / sizeof(check[0]); i++) { 
		if (stack_area_virtual_base() <= check[i] &&
			check[i] < stack_area_virtual_base() + stack_area_virtual_size())
		{
			error("overlapping area - ",
			      Hex_range<addr_t>(stack_area_virtual_base(),
			                        stack_area_virtual_size()), " vs ",
			      Hex(check[i]));

			error_overlap = true;
		}
	}
 
	/* initialize core's physical-memory and I/O memory allocator */
	_io_mem_alloc.add_range(0, ~0xfffUL);

	Boot_info boot_info = { };

	error_memory = setup_allocators(_io_mem_alloc, ram_alloc(), region_alloc(),
	                                _core_phys_start, boot_info);

	/* remove reserved RAM regions occupied by kernel */
	ram_alloc().remove_range(hip.nova_addr_start, hip.nova_addr_end - hip.nova_addr_start);
	ram_alloc().remove_range(hip.mbuf_addr_start, hip.mbuf_addr_end - hip.mbuf_addr_start);
	ram_alloc().remove_range(hip.root_addr_start, hip.root_addr_end - hip.root_addr_start);

	/* needed as I/O memory by the VESA driver and acpi to search for rsdp */
	_io_mem_alloc.add_range   (0, 0x2000);
	ram_alloc()  .remove_range(0, 0x2000);

	/*
	 * From now on, it is save to use the core allocators...
	 */

	size_t kernel_memory = hip.nova_addr_end - hip.nova_addr_start;

	_init_rom_modules();

	auto export_pages_as_rom_module = [&] (auto rom_name, size_t pages, auto content_fn)
	{
		size_t const bytes = pages << get_page_size_log2();
		ram_alloc().alloc_aligned(bytes, get_page_size_log2()).with_result(

			[&] (void *phys_ptr) {

				addr_t const phys_addr = reinterpret_cast<addr_t>(phys_ptr);
				char * const core_local_ptr = (char *)_map_pages(phys_addr, pages);

				if (!core_local_ptr) {
					warning("failed to export ", rom_name, " as ROM module");
					ram_alloc().free(phys_ptr, bytes);
					return;
				}

				memset(core_local_ptr, 0, bytes);

				content_fn(core_local_ptr, bytes);

				new (core_mem_alloc())
					Rom_module(_rom_fs, rom_name, phys_addr, bytes);

				/* leave the ROM backing store mapped within core */
			},

			[&] (Range_allocator::Alloc_error) {
				warning("failed to allocate physical memory for exporting ",
				        rom_name, " as ROM module"); });
	};

	export_pages_as_rom_module("platform_info", 1 + (MAX_SUPPORTED_CPUS / 32),
		[&] (char * const ptr, size_t const size) {
			Xml_generator xml(ptr, size, "platform_info", [&]
			{
				xml.node("kernel", [&] {
					xml.attribute("name", "novae");
					xml.attribute("acpi", true);
					xml.attribute("msi" , true);
					xml.attribute("iommu", hip.has_feature_iommu());
				});
				if (boot_info.efi_sys_tab_phy) {
					xml.node("efi-system-table", [&] {
						xml.attribute("address", String<32>(Hex(boot_info.efi_sys_tab_phy)));
					});
				}
				xml.node("acpi", [&] {

					xml.attribute("revision", 2); /* XXX */

					if (boot_info.rsdt)
						xml.attribute("rsdt", String<32>(Hex(boot_info.rsdt)));

					if (boot_info.xsdt)
						xml.attribute("xsdt", String<32>(Hex(boot_info.xsdt)));
				});
				xml.node("affinity-space", [&] {
					xml.attribute("width", _cpus.width());
					xml.attribute("height", _cpus.height());
				});
				xml.node("boot", [&] {
					if (!boot_info.efi_boot && (boot_info.fb.type != 2 /* VGA_TEXT */))
						return;

					xml.node("framebuffer", [&] {
						xml.attribute("phys",   String<32>(Hex(boot_info.fb.addr)));
						xml.attribute("width",  boot_info.fb.width);
						xml.attribute("height", boot_info.fb.height);
						xml.attribute("bpp",    boot_info.fb.bpp);
						xml.attribute("type",   boot_info.fb.type);
						xml.attribute("pitch",  boot_info.fb.pitch);
					});
				});
				xml.node("hardware", [&] {
					xml.node("features", [&] {
						xml.attribute("svm", hip.has_feature_svm());
						xml.attribute("vmx", hip.has_feature_vmx());
					});
					xml.node("tsc", [&] {
						xml.attribute("invariant", cpuid_invariant_tsc());
						xml.attribute("freq_hz"  , hip.timer_freq);
						xml.attribute("freq_khz" , hip.timer_freq / 1000);
					});
					xml.node("cpus", [&] {
						for_each_location([&](Affinity::Location &location) {
							unsigned const kernel_cpu_id = Platform::kernel_cpu_id(location);

							xml.node("cpu", [&] {
								xml.attribute("xpos",     location.xpos());
								xml.attribute("ypos",     location.ypos());
								xml.attribute("id",       kernel_cpu_id);
							});
						});
					});
				});
			});
		}
	);

	export_pages_as_rom_module("core_log", 4,
		[&] (char * const ptr, size_t const size) {
			init_core_log( Core_log_range { (addr_t)ptr, size } );
	});

	/* export hypervisor log memory */
	if (hip.mbuf_addr_start && hip.mbuf_addr_end)
		new (core_mem_alloc())
			Rom_module(_rom_fs, "kernel_log", hip.mbuf_addr_start,
			           hip.mbuf_addr_end - hip.mbuf_addr_start);

	/* show all warnings/errors after init_core_log setup core_log */
	if (warn_reorder)
		warning("re-ordering of CPU ids for SMT and P/E cores failed");
	if (binaries_end != core_virt_end)
		error("mismatch in address layout of binaries with core");
	if (error_overlap)
		error("memory overlap issues detected");
	if (hip.sel_hst_arch + 3 > NUM_INITIAL_PT_RESERVED)
		error("configuration error (NUM_INITIAL_PT_RESERVED)");
	if (error_memory)
		error("Memory allocator issues detected");
	if (error_remap)
		error("UTCB of first thread could not be remapped");

	/* map idle SCs */
	auto const log2cpu  = log2(unsigned(hip.cpu_num));
	auto const idle_scs = cap_map().insert(log2cpu + 1);

	if (async_map(hip.sel_num - 1, /* kernel object space */
	              hip.sel_num - 2, /* root   object space */
	              Obj_crd(       0, log2cpu),
	              Obj_crd(idle_scs, log2cpu)))
		error("idle SC information unavailable");

	if (verbose_boot_info) {
		if (hip.has_feature_iommu())
			log("Hypervisor features IOMMU");
		if (hip.has_feature_vmx())
			log("Hypervisor features VMX");
		if (hip.has_feature_svm())
			log("Hypervisor features SVM");
		log("Hypervisor reports ", _cpus.width(), "x", _cpus.height(), " "
		    "CPU", _cpus.total() > 1 ? "s" : " ");
		if (!cpuid_invariant_tsc())
			warning("CPU has no invariant TSC.");

		log("mapping: affinity space -> kernel cpu id - package:core:thread");

		for_each_location([&](Affinity::Location &location) {
			unsigned const kernel_cpu_id = Platform::kernel_cpu_id(location);

			Genode::String<16> text ("unknown");

			log(" remap (", location.xpos(), "x", location.ypos(),") -> ",
			    kernel_cpu_id, " - ", text,
			    hip.cpu_bsp == kernel_cpu_id ? " boot cpu" : "");
		});
	}

	/* I/O port allocator (only meaningful for x86) */
	_io_port_alloc.add_range(0, 0x10000);

	/* IRQ allocator */
	_irq_alloc.add_range(0, hip.int_pin + hip.int_msi);

	if (verbose_boot_info)
		log(_rom_fs);

	log(Number_of_bytes(kernel_memory), " kernel memory"); log("");


	/* add capability selector ranges to map */
	auto const idx_start = 0x2000;
	auto       index     = idx_start;

	for (unsigned i = 0; i < 32; i++)
	{
		void * phys_ptr = nullptr;

		ram_alloc().alloc_aligned(get_page_size(), get_page_size_log2()).with_result(
			[&] (void *ptr) { phys_ptr = ptr; },
			[&] (Range_allocator::Alloc_error) { /* covered by nullptr test below */ });

		if (phys_ptr == nullptr)
			break;

		addr_t phys_addr = reinterpret_cast<addr_t>(phys_ptr);
		addr_t core_local_addr = _map_pages(phys_addr, 1);

		if (!core_local_addr) {
			ram_alloc().free(phys_ptr);
			break;
		}

		Cap_range &range = *reinterpret_cast<Cap_range *>(core_local_addr);
		construct_at<Cap_range>(&range, index);

		cap_map().insert(range);

		index = (unsigned)(range.base() + range.elements());
	}

	_max_caps = index - idx_start;

	/* add idle ECs to trace sources */
	for_each_location([&](Affinity::Location &location) {
		unsigned const kernel_cpu_id = Platform::kernel_cpu_id(location);

		struct Trace_source : public  Trace::Source::Info_accessor,
		                      private Trace::Control,
		                      private Trace::Source
		{
			Affinity::Location const affinity;
			unsigned           const sc_sel;
			Genode::String<8>  const name;

			/**
			 * Trace::Source::Info_accessor interface
			 */
			Info trace_source_info() const override
			{
				uint64_t sc_time = 0;

				uint8_t res = Novae::sc_ctrl(sc_sel, sc_time);

				if (res != Novae::NOVA_OK)
					warning("sc_ctrl on ", name, " failed"
					        ", res=", res);

				return { Session_label("kernel"), Trace::Thread_name(name),
				         Trace::Execution_time(0, sc_time), affinity };
			}

			Trace_source(Trace::Source_registry &registry,
			             Affinity::Location const affinity,
			             unsigned const sc_sel,
			             char const * type_name)
			:
				Trace::Control(),
				Trace::Source(*this, *this), affinity(affinity),
				sc_sel(sc_sel), name(type_name)
			{
				registry.insert(this);
			}
		};

		new (core_mem_alloc()) Trace_source(Trace::sources(), location,
		                                    (unsigned)(idle_scs + kernel_cpu_id),
		                                    "idle");
	});

	/* add exception handler EC for core and EC root thread to trace sources */
	struct Core_trace_source : public  Trace::Source::Info_accessor,
	                           private Trace::Control,
	                           private Trace::Source
	{
		Affinity::Location const location;
		addr_t             const sc_sel;
		Genode::String<8>  const name;

		/**
		 * Trace::Source::Info_accessor interface
		 */
		Info trace_source_info() const override
		{
			uint64_t sc_time = 0;

			uint8_t res = Novae::sc_ctrl(sc_sel, sc_time);
			if (res != Novae::NOVA_OK)
				warning("sc_ctrl for root failed res=", res);

			return { Session_label("core"), name,
			         Trace::Execution_time(0, sc_time), location };
		}

		Core_trace_source(Trace::Source_registry &registry,
		                  Affinity::Location loc, addr_t sel,
		                  char const *name)
		:
			Trace::Control(),
			Trace::Source(*this, *this), location(loc), sc_sel(sel),
			name(name)
		{
			registry.insert(this);
		}
	};

	new (core_mem_alloc())
		Core_trace_source(Trace::sources(),
		                  Affinity::Location(0, 0, _cpus.width(), 1),
		                  hip.sel_num - 5, "root");
}


addr_t Core::Platform::_rom_module_phys(addr_t virt)
{
	return virt - (addr_t)&_prog_img_beg + _core_phys_start;
}


unsigned Core::Platform::kernel_cpu_id(Affinity::Location location) const
{
	unsigned const cpu_id = pager_index(location);

	if (cpu_id >= sizeof(map_cpu_ids) / sizeof(map_cpu_ids[0])) {
		error("invalid genode cpu id ", cpu_id);
		return ~0U;
	}

	return map_cpu_ids[cpu_id];
}


unsigned Core::Platform::pager_index(Affinity::Location location) const
{
	return (location.xpos() * _cpus.height() + location.ypos())
	       % (_cpus.width() * _cpus.height());
}


/****************************************
 ** Support for core memory management **
 ****************************************/

bool Mapped_mem_allocator::_map_local(addr_t virt_addr, addr_t phys_addr, size_t size)
{
	auto res = map_local(Platform::kernel_host_sel(),
	                     Platform::core_host_sel(),
	                     phys_addr, virt_addr, size / get_page_size(),
	                     Rights::rw());

	if (res != Novae::NOVA_OK)
		error(__func__, " check me ", Hex(phys_addr), "->", Hex(virt_addr), "+", Hex(size));

	return res == Novae::NOVA_OK;
}


bool Mapped_mem_allocator::_unmap_local(addr_t virt_addr, addr_t, size_t size)
{
	unmap_local(platform_specific().core_host_sel(), virt_addr,
	            size / get_page_size(), Rights::none());

	return true;
}


/********************************
 ** Generic platform interface **
 ********************************/

void Core::Platform::wait_for_exit() { sleep_forever(); }

