/*
 * \brief  Broadwell multiplexer
 * \author Josef Soentgen
 * \data   2017-03-15
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/ram_allocator.h>
#include <base/registry.h>
#include <base/rpc_server.h>
#include <base/session_object.h>
#include <dataspace/client.h>
#include <gpu_session/gpu_session.h>
#include <io_mem_session/connection.h>
#include <irq_session/connection.h>
#include <platform_device/client.h>
#include <platform_session/connection.h>
#include <root/component.h>
#include <timer_session/connection.h>
#include <util/fifo.h>
#include <util/mmio.h>
#include <util/retry.h>
#include <util/xml_node.h>

/* local includes */
#include <mmio.h>
#include <ppgtt.h>
#include <ppgtt_allocator.h>
#include <ggtt.h>
#include <context.h>
#include <context_descriptor.h>
#include <ring_buffer.h>


using namespace Genode;


namespace Igd {

	struct Device_info;
	struct Device;
}


struct Igd::Device_info
{
	uint16_t    id;
	char const *descr;
	uint64_t    features;
};


/*
 * IHD-OS-BDW-Vol 4-11.15 p. 9
 */
static Igd::Device_info _supported_devices[] = {
	{ 0x1606, "HD Graphics (BDW GT1 ULT)", 0ull },
	{ 0x1616, "HD Graphics 5500 (BDW GT2 ULT)", 0ull },
	/* TODO proper eDRAM probing + caching */
	{ 0x1622, "Iris Pro Graphics 6200 (BDW GT3e)", 0ull },
};

#define ELEM_NUMBER(x) (sizeof((x)) / sizeof((x)[0]))


struct Igd::Device
{
	struct Initialization_failed : Genode::Exception { };
	struct Unsupported_device    : Genode::Exception { };
	struct Out_of_ram            : Genode::Exception { };
	struct Already_scheduled     : Genode::Exception { };
	struct Could_not_map_buffer  : Genode::Exception { };

	Env       &_env;
	Allocator &_md_alloc;

	/***********
	 ** Timer **
	 ***********/

	Timer::Connection         _timer { _env };

	enum { WATCHDOG_TIMEOUT = 1*1000*1000, };

	struct Timer_delayer : Genode::Mmio::Delayer
	{
		Timer::Connection &_timer;
		Timer_delayer(Timer::Connection &timer) : _timer(timer) { }

		void usleep(uint64_t us) override { _timer.usleep(us); }

	} _delayer { _timer };

	/*********
	 ** PCI **
	 *********/

	/*
	 * Config space utility methods
	 */

	template <typename T>
	static Platform::Device::Access_size _access_size(T)
	{
		switch (sizeof(T)) {
			case 1:  return Platform::Device::ACCESS_8BIT;
			case 2:  return Platform::Device::ACCESS_16BIT;
			default: return Platform::Device::ACCESS_32BIT;
		}
	}

	template <typename FUNC>
	void _retry_func(FUNC const &func)
	{
		Genode::size_t donate = PAGE_SIZE;
		Genode::retry<Platform::Out_of_ram>(
			func,
			[&] () {
				_pci.upgrade_ram(donate);
				donate *= 2;
			}, 2);
	}

	template <typename T>
	T _config_read(unsigned int const devfn)
	{
		T val = 0;
		_retry_func([&] () {
			val = _device.config_read(devfn, _access_size(val));
		});

		return val;
	}

	template <typename T>
	void _config_write(unsigned int const devfn, T val)
	{
		_retry_func([&] () {
			_device.config_write(devfn, val, _access_size(val));
		});
	}

	Platform::Connection        &_pci;
	Platform::Device_capability  _pci_cap;
	Platform::Device_client      _device { _pci_cap };

	struct Pci_backend_alloc : Utils::Backend_alloc
	{
		Platform::Connection &_pci;

		Pci_backend_alloc(Platform::Connection &pci) : _pci(pci) { }

		Ram_dataspace_capability alloc(size_t size) override
		{
			return _pci.with_upgrade([&] () {
				return _pci.alloc_dma_buffer(size, Genode::UNCACHED); });
		}

		void free(Ram_dataspace_capability cap) override
		{
			if (!cap.valid()) {
				Genode::error("could not free, capability invalid");
				return;
			}

			_pci.free_dma_buffer(cap);
		}

	} _pci_backend_alloc { _pci };

	enum {
		PCI_NUM_RES    = 6,
		PCI_CMD_REG    = 4,
		PCI_BUS_MASTER = 1<<2,

		GTTMMADR = 0,
		GMADR    = 2,
	};
	Genode::Constructible<Genode::Io_mem_connection> _res[PCI_NUM_RES];
	addr_t                                           _res_base[PCI_NUM_RES];
	size_t                                           _res_size[PCI_NUM_RES];
	Genode::Io_mem_dataspace_capability              _res_ds[PCI_NUM_RES];

	Genode::Constructible<Genode::Irq_session_client> _irq { };

	void _poke_pci_resource(unsigned const id)
	{
		if (id >= PCI_NUM_RES)      { throw -1; }
		if (_res[id].constructed()) { throw -2; }

		Platform::Device::Resource const res = _device.resource(id);
		_res_base[id] = res.base();
		_res_size[id] = res.size();
	}

	addr_t _map_pci_resource(unsigned const id)
	{
		_poke_pci_resource(id);

		_res[id].construct(_env, _res_base[id], _res_size[id]);
		_res_ds[id] = _res[id]->dataspace();
		if (!_res_ds[id].valid()) { throw Initialization_failed(); }

		addr_t addr = (addr_t)(_env.rm().attach(_res_ds[id], _res_size[id]));

		using namespace Genode;
		log("Map res:", id,
		    " base:", Hex(_res_base[id]),
		    " size:", Hex(_res_size[id]),
		    " vaddr:", Hex(addr));

		return addr;
	}

	void _enable_pci_bus_master()
	{
		uint16_t cmd  = _config_read<uint16_t>(PCI_CMD_REG);
		         cmd |= PCI_BUS_MASTER;
		_config_write(PCI_CMD_REG, cmd);
	}

	Device_info _info { };

	void _pci_info(char const *descr)
	{
		using namespace Genode;

		uint16_t const vendor_id  = _device.vendor_id();
		uint16_t const device_id  = _device.device_id();

		uint8_t bus = 0, dev = 0, fun = 0;
		_device.bus_address(&bus, &dev, &fun);

		log("Found: '", descr, "' ",
		    "[", Hex(vendor_id), ":", Hex(device_id), "] (",
		    Hex(bus, Hex::OMIT_PREFIX), ":",
		    Hex(dev, Hex::OMIT_PREFIX), ".",
		    Hex(fun, Hex::OMIT_PREFIX), ")");

		for (int i = 0; i < PCI_NUM_RES; i++) {

			using Resource = Platform::Device::Resource;

			Resource const resource = _device.resource(i);

			if (resource.type() == Resource::INVALID) { continue; }

			log("  Resource ", i, " "
			    "(", resource.type() == Resource::IO ? "I/O" : "MEM", "): "
			    "base=", Genode::Hex(resource.base()), " "
			    "size=", Genode::Hex(resource.size()), " ",
			    (resource.prefetchable() ? "prefetchable" : ""));
		}
	}

	bool _supported()
	{
		uint16_t const id = _device.device_id();
		for (size_t i = 0; i < ELEM_NUMBER(_supported_devices); i++) {
			if (_supported_devices[i].id == id) {
				_info = _supported_devices[i];
				_pci_info(_supported_devices[i].descr);
				return true;
			}
		}
		_pci_info("<Unsupported device>");
		return false;
	}

	/**********
	 ** GGTT **
	 **********/

	Genode::Constructible<Igd::Ggtt> _ggtt { };

	/**********
	 ** MMIO **
	 **********/

	Genode::Constructible<Igd::Mmio> _mmio { };

	/************
	 ** MEMORY **
	 ************/

	struct Unaligned_size : Genode::Exception { };

	Ram_dataspace_capability _alloc_dataspace(size_t const size)
	{
		if (size & 0xfff) { throw Unaligned_size(); }

		Genode::Ram_dataspace_capability ds = _pci_backend_alloc.alloc(size);
		if (!ds.valid()) { throw Out_of_ram(); }
		return ds;
	}

	void free_dataspace(Ram_dataspace_capability const cap)
	{
		if (!cap.valid()) { return; }

		_pci_backend_alloc.free(cap);
	}

	struct Ggtt_mmio_mapping : Ggtt::Mapping
	{
		Genode::Io_mem_connection mmio;

		Ggtt_mmio_mapping(Genode::Env &env, addr_t base, size_t size,
		                  Ggtt::Offset offset)
		:
			mmio(env, base, size)
		{
			Ggtt::Mapping::cap    = mmio.dataspace();
			Ggtt::Mapping::offset = offset;
		}

		virtual ~Ggtt_mmio_mapping() { }
	};

	Genode::Registry<Genode::Registered<Ggtt_mmio_mapping>> _ggtt_mmio_mapping_registry { };

	Ggtt_mmio_mapping const &map_dataspace_ggtt(Genode::Allocator &alloc,
	                                            Genode::Dataspace_capability cap,
	                                            Ggtt::Offset offset)
	{
		Genode::Dataspace_client client(cap);
		addr_t const phys_addr = client.phys_addr();
		size_t const size      = client.size();

		/*
		 * Create the mapping first and insert the entries afterwards
		 * so we do not have to rollback when the allocation failes.
		 */

		addr_t const base = _res_base[GMADR] + _ggtt->addr(offset);
		Genode::Registered<Ggtt_mmio_mapping> *mem = new (&alloc)
			Genode::Registered<Ggtt_mmio_mapping>(_ggtt_mmio_mapping_registry,
			                                      _env, base, size, offset);

		for (size_t i = 0; i < size; i += PAGE_SIZE) {
			addr_t const pa = phys_addr + i;
			_ggtt->insert_pte(pa, offset + (i / PAGE_SIZE));
		}

		return *mem;
	}

	void unmap_dataspace_ggtt(Genode::Allocator &alloc, Genode::Dataspace_capability cap)
	{
		size_t const num = Genode::Dataspace_client(cap).size() / PAGE_SIZE;

		auto lookup_and_free = [&] (Ggtt_mmio_mapping &m) {
			if (!(m.cap == cap)) { return; }

			_ggtt->remove_pte_range(m.offset, num);
			Genode::destroy(&alloc, &m);
		};

		_ggtt_mmio_mapping_registry.for_each(lookup_and_free);
	}

	struct Invalid_ppgtt : Genode::Exception { };

	static addr_t _ppgtt_phys_addr(Igd::Ppgtt_allocator &alloc,
	                               Igd::Ppgtt const * const ppgtt)
	{
		void * const p = alloc.phys_addr(const_cast<Igd::Ppgtt*>(ppgtt));
		if (p == nullptr) { throw Invalid_ppgtt(); }
		return reinterpret_cast<addr_t>(p);
	}

	/************
	 ** ENGINE **
	 ************/

	struct Execlist : Genode::Noncopyable
	{
		Igd::Context_descriptor _elem0;
		Igd::Context_descriptor _elem1;

		Igd::Ring_buffer        _ring;
		bool                    _scheduled;

		Execlist(uint32_t const id, addr_t const lrca,
		         addr_t const ring, size_t const ring_size)
		:
			_elem0(id, lrca), _elem1(),
			_ring(ring, ring_size), _scheduled(false)
		{ }

		Igd::Context_descriptor elem0() const { return _elem0; }
		Igd::Context_descriptor elem1() const { return _elem1; }

		void schedule(int port) { _scheduled = port; }
		int scheduled() const { return _scheduled; }

		/***************************
		 ** Ring buffer interface **
		 ***************************/

		void               ring_reset() { _ring.reset(); }
		Ring_buffer::Index ring_tail() const { return _ring.tail(); }
		Ring_buffer::Index ring_head() const { return _ring.head(); }
		Ring_buffer::Index ring_append(Igd::Cmd_header cmd) { return _ring.append(cmd); }

		bool ring_avail(Ring_buffer::Index num) const { return _ring.avail(num); }
		Ring_buffer::Index ring_max()   const { return _ring.max(); }
		void ring_reset_and_fill_zero() { _ring.reset_and_fill_zero(); }
		void ring_update_head(Ring_buffer::Index head) { _ring.update_head(head); }

		void ring_flush(Ring_buffer::Index from, Ring_buffer::Index to)
		{
			_ring.flush(from, to);
		}

		void ring_dump(size_t limit = 0) const { _ring.dump(limit); }

		/*********************
		 ** Debug interface **
		 *********************/

		void dump() { _elem0.dump(); }
	};

	struct Ggtt_map_memory
	{
		struct Dataspace_guard {
			Device                         &device;
			Ram_dataspace_capability const  ds;

			Dataspace_guard(Device &device, Ram_dataspace_capability ds)
			: device(device), ds(ds)
			{ }

			~Dataspace_guard()
			{
				if (ds.valid())
					device.free_dataspace(ds);
			}
		};

		struct Mapping_guard {
			Device              &device;
			Allocator           &alloc;
			Ggtt::Mapping const &map;

			Mapping_guard(Device &device, Ggtt_map_memory &gmm, Allocator &alloc)
			:
				device(device),
				alloc(alloc),
				map(device.map_dataspace_ggtt(alloc, gmm.ram_ds.ds, gmm.offset))
			{ }

			~Mapping_guard() { device.unmap_dataspace_ggtt(alloc, map.cap); }
		};

		Device             const &device;
		Allocator          const &alloc;
		Ggtt::Offset       const  offset;
		Ggtt::Offset       const  skip;
		Dataspace_guard    const  ram_ds;
		Mapping_guard      const  map;
		Attached_dataspace const  ram;

		addr_t vaddr() const
		{
			return reinterpret_cast<addr_t>(ram.local_addr<addr_t>())
			       + (skip * PAGE_SIZE);
		}

		addr_t gmaddr() const { /* graphics memory address */
			return (offset + skip) * PAGE_SIZE; }

		Ggtt_map_memory(Region_map &rm, Allocator &alloc, Device &device,
		                Ggtt::Offset const pages, Ggtt::Offset const skip_pages)
		:
			device(device),
			alloc(alloc),
			offset(device._ggtt->find_free(pages, true)),
			skip(skip_pages),
			ram_ds(device, device._alloc_dataspace(pages * PAGE_SIZE)),
			map(device, *this, alloc),
			ram(rm, map.map.cap)
		{ }
	};

	enum { HWS_OFFSET_DATA = 0xc0 };

	template <typename CONTEXT>
	struct Engine
	{
		enum {
			CONTEXT_PAGES = CONTEXT::CONTEXT_PAGES,
			RING_PAGES    = CONTEXT::RING_PAGES,
		};

		Ggtt_map_memory ctx;  /* context memory */
		Ggtt_map_memory ring; /* ring memory */

		Ppgtt_allocator  ppgtt_allocator;
		Ppgtt_scratch    ppgtt_scratch;
		Ppgtt           *ppgtt { nullptr };

		Genode::Constructible<CONTEXT>  context  { };
		Genode::Constructible<Execlist> execlist { };

		Engine(Igd::Device         &device,
		       uint32_t             id,
		       Allocator           &alloc)
		:
			ctx (device._env.rm(), alloc, device, CONTEXT::CONTEXT_PAGES, 1 /* omit GuC page */),
			ring(device._env.rm(), alloc, device, CONTEXT::RING_PAGES, 0),
			ppgtt_allocator(device._env.rm(), device._pci_backend_alloc),
			ppgtt_scratch(device._pci_backend_alloc)
		{
			/* PPGTT */
			device._populate_scratch(ppgtt_scratch);

			ppgtt = new (ppgtt_allocator) Igd::Ppgtt(&ppgtt_scratch.pdp);

			ppgtt_scratch.check_dump(device._env);

			try {
				size_t const ring_size = RING_PAGES * PAGE_SIZE;

				/* get PML4 address */
				addr_t const ppgtt_phys_addr = _ppgtt_phys_addr(ppgtt_allocator,
				                                                ppgtt);
				addr_t const pml4 = ppgtt_phys_addr | 1;

				/* setup context */
				context.construct(ctx.vaddr(), ring.gmaddr(), ring_size, pml4);

				/* setup execlist */
				execlist.construct(id, ctx.gmaddr(), ring.vaddr(), ring_size);
				execlist->ring_reset();
			} catch (...) {
				_destruct();
				throw;
			}
		}

		~Engine() { _destruct(); };

		void _destruct()
		{
			if (ppgtt)
				Genode::destroy(ppgtt_allocator, ppgtt);

			execlist.destruct();
			context.destruct();
		}

		size_t ring_size() const { return RING_PAGES * PAGE_SIZE; }

		addr_t hw_status_page() const { return ctx.gmaddr(); }

		uint64_t seqno() const {
			return *(volatile uint64_t*)(ctx.vaddr() + HWS_OFFSET_DATA); }

		private:

			/*
			 * Noncopyable
			 */
			Engine(Engine const &);
			Engine &operator = (Engine const &);
	};

	void _fill_page(Genode::Ram_dataspace_capability ds, addr_t v)
	{
		Attached_dataspace ram(_env.rm(), ds);
		uint64_t * const p = ram.local_addr<uint64_t>();

		for (size_t i = 0; i < Ppgtt_scratch::MAX_ENTRIES; i++) {
			p[i] = v;
		}
	}

	void _populate_scratch(Ppgtt_scratch &scratch)
	{
		_fill_page(scratch.pt.ds,  scratch.page.addr);
		_fill_page(scratch.pd.ds,  scratch.pt.addr);
		_fill_page(scratch.pdp.ds, scratch.pd.addr);
	}


	/**********
	 ** Vgpu **
	 **********/

	uint32_t _vgpu_avail { 0 };

	struct Vgpu : Genode::Fifo<Vgpu>::Element
	{
		enum {
			APERTURE_SIZE = 32u << 20,
			MAX_FENCES    = 4,
		};

		Device                          &_device;
		Signal_context_capability        _completion_sigh { };
		uint32_t                  const  _id;
		Engine<Rcs_context>              rcs;
		uint32_t                         active_fences  { 0 };
		uint64_t                         _current_seqno { 0 };

		uint32_t _id_alloc()
		{
			static uint32_t id = 1;

			uint32_t const v = id++;
			return v << 8;
		}

		Vgpu(Device &device, Allocator &alloc)
		:
			_device(device),
			_id(_id_alloc()),
			rcs(_device, _id + Rcs_context::HW_ID, alloc)
		{
			_device.vgpu_created();
		}

		~Vgpu()
		{
			_device.vgpu_released();
		}

		uint32_t id() const { return _id; }

		void completion_sigh(Genode::Signal_context_capability sigh) {
			_completion_sigh = sigh; }

		Genode::Signal_context_capability completion_sigh() {
			return _completion_sigh; }

		uint64_t current_seqno() const { return _current_seqno; }

		uint64_t complete_seqno() const { return rcs.seqno(); }

		void setup_ring_buffer(Genode::addr_t const buffer_addr,
		                       Genode::addr_t const scratch_addr)
		{
			_current_seqno++;

			Execlist &el = *rcs.execlist;

			Ring_buffer::Index advance = 0;

			size_t const need = 4 /* batchbuffer cmd */ + 6 /* prolog */ + 16 /* epilog + w/a */;
			if (!el.ring_avail(need)) { el.ring_reset_and_fill_zero(); }

			/* save old tail */
			Ring_buffer::Index const tail = el.ring_tail();

			/*
			 * IHD-OS-BDW-Vol 7-11.15 p. 18 ff.
			 *
			 * Pipeline synchronization
			 */

			/* prolog */
			if (1)
			{
				enum { CMD_NUM = 6 };
				Genode::uint32_t cmd[CMD_NUM] = {};
				Igd::Pipe_control pc(CMD_NUM);
				cmd[0] = pc.value;
				Genode::uint32_t tmp = 0;
				tmp |= Igd::Pipe_control::CS_STALL;
				tmp |= Igd::Pipe_control::TLB_INVALIDATE;
				tmp |= Igd::Pipe_control::INSTRUCTION_CACHE_INVALIDATE;
				tmp |= Igd::Pipe_control::TEXTURE_CACHE_INVALIDATE;
				tmp |= Igd::Pipe_control::VF_CACHE_INVALIDATE;
				tmp |= Igd::Pipe_control::CONST_CACHE_INVALIDATE;
				tmp |= Igd::Pipe_control::STATE_CACHE_INVALIDATE;
				tmp |= Igd::Pipe_control::QW_WRITE;
				tmp |= Igd::Pipe_control::GLOBAL_GTT_IVB;
				tmp |= Igd::Pipe_control::DC_FLUSH_ENABLE;
				tmp |= Igd::Pipe_control::INDIRECT_STATE_DISABLE;
				tmp |= Igd::Pipe_control::MEDIA_STATE_CLEAR;

				cmd[1] = tmp;
				cmd[2] = scratch_addr;
				cmd[3] = 0;
				cmd[4] = 0;
				cmd[5] = 0;

				for (size_t i = 0; i < CMD_NUM; i++) {
					advance += el.ring_append(cmd[i]);
				}
			}

			/* batch-buffer commands */
			if (1)
			{
				enum { CMD_NUM = 4, };
				Genode::uint32_t cmd[CMD_NUM] = {};
				Igd::Mi_batch_buffer_start mi;

				cmd[0] = mi.value;
				cmd[1] = buffer_addr & 0xffffffff;
				cmd[2] = (buffer_addr >> 32) & 0xffff;
				cmd[3] = 0; /* MI_NOOP */

				for (size_t i = 0; i < CMD_NUM; i++) {
					advance += el.ring_append(cmd[i]);
				}
			}

			/* epilog */
			if (1)
			{
				enum { CMD_NUM = 6 };
				Genode::uint32_t cmd[CMD_NUM] = {};
				Igd::Pipe_control pc(CMD_NUM);
				cmd[0] = pc.value;
				Genode::uint32_t tmp = 0;
				tmp |= Igd::Pipe_control::CS_STALL;
				tmp |= Igd::Pipe_control::RENDER_TARGET_CACHE_FLUSH;
				tmp |= Igd::Pipe_control::DEPTH_CACHE_FLUSH;
				tmp |= Igd::Pipe_control::DC_FLUSH_ENABLE;
				tmp |= Igd::Pipe_control::FLUSH_ENABLE;

				cmd[1] = tmp;
				cmd[2] = scratch_addr;
				cmd[3] = 0;
				cmd[4] = 0;
				cmd[5] = 0;

				for (size_t i = 0; i < CMD_NUM; i++) {
					advance += el.ring_append(cmd[i]);
				}
			}

			/*
			 * IHD-OS-BDW-Vol 2d-11.15 p. 199 ff.
			 *
			 * HWS page layout dword 48 - 1023 for driver usage
			 */
			if (1)
			{
				enum { CMD_NUM = 8 };
				Genode::uint32_t cmd[8] = {};
				Igd::Pipe_control pc(6);
				cmd[0] = pc.value;
				Genode::uint32_t tmp = 0;
				tmp |= Igd::Pipe_control::GLOBAL_GTT_IVB;
				tmp |= Igd::Pipe_control::CS_STALL;
				tmp |= Igd::Pipe_control::QW_WRITE;
				cmd[1] = tmp;
				cmd[2] = (rcs.hw_status_page() + HWS_OFFSET_DATA) & 0xffffffff;
				cmd[3] = 0; /* upper addr 0 */
				cmd[4] = _current_seqno & 0xffffffff;
				cmd[5] = _current_seqno >> 32;
				Igd::Mi_user_interrupt ui;
				cmd[6] = ui.value;
				cmd[7] = 0; /* MI_NOOP */

				for (size_t i = 0; i < CMD_NUM; i++) {
					advance += el.ring_append(cmd[i]);
				}
			}

			/* w/a */
			if (1)
			{
				enum { CMD_NUM = 2, };
				for (size_t i = 0; i < CMD_NUM; i++) {
					advance += el.ring_append(0);
				}
			}

			addr_t const offset = (((tail + advance) * sizeof(uint32_t)) >> 3) - 1;
			rcs.context->tail_offset(offset % ((4*4096)>>3));
		}

		void rcs_map_ppgtt(addr_t vo, addr_t pa, size_t size)
		{
			Genode::Page_flags pf;
			pf.writeable = Genode::Writeable::RW;

			try {
				rcs.ppgtt->insert_translation(vo, pa, size, pf,
				                              &rcs.ppgtt_allocator,
				                              &rcs.ppgtt_scratch.pdp);
				_device._mmio->flush_gfx_tlb();
			} catch (Igd::Ppgtt_allocator::Out_of_memory) {
				throw Igd::Device::Out_of_ram();
			} catch (...) {
				/* Double_insertion and the like */
				throw Igd::Device::Could_not_map_buffer();
			}
		}

		void rcs_unmap_ppgtt(addr_t vo, size_t size)
		{
			rcs.ppgtt->remove_translation(vo, size,
			                              &rcs.ppgtt_allocator,
			                              &rcs.ppgtt_scratch.pdp);
			_device._mmio->flush_gfx_tlb();
		}
	};

	/****************
	 ** SCHEDULING **
	 ****************/

	Genode::Fifo<Vgpu>  _vgpu_list { };
	Vgpu               *_active_vgpu { nullptr };

	bool _vgpu_already_scheduled(Vgpu &vgpu) const
	{
		bool result = false;
		_vgpu_list.for_each([&] (Vgpu const &v) {
			result |= (&v == &vgpu); });
		return result;
	}

	void _submit_execlist(Engine<Rcs_context> &engine)
	{
		Execlist &el = *engine.execlist;

		int const port = _mmio->read<Igd::Mmio::EXECLIST_STATUS_RSCUNIT::Execlist_write_pointer>();

		el.schedule(port);

		uint32_t desc[4];
		desc[3] = el.elem1().high();
		desc[2] = el.elem1().low();
		desc[1] = el.elem0().high();
		desc[0] = el.elem0().low();

		_mmio->write<Igd::Mmio::EXECLIST_SUBMITPORT_RSCUNIT>(desc[3]);
		_mmio->write<Igd::Mmio::EXECLIST_SUBMITPORT_RSCUNIT>(desc[2]);
		_mmio->write<Igd::Mmio::EXECLIST_SUBMITPORT_RSCUNIT>(desc[1]);
		_mmio->write<Igd::Mmio::EXECLIST_SUBMITPORT_RSCUNIT>(desc[0]);
	}

	Vgpu *_unschedule_current_vgpu()
	{
		Vgpu *result = nullptr;
		_vgpu_list.dequeue([&] (Vgpu &head) {
			result = &head; });
		return result;
	}

	Vgpu *_current_vgpu()
	{
		Vgpu *result = nullptr;
		_vgpu_list.head([&] (Vgpu &head) {
			result = &head; });
		return result;
	}

	void _schedule_current_vgpu()
	{
		Vgpu *gpu = _current_vgpu();
		if (!gpu) {
			Genode::warning("no valid vGPU for scheduling found.");
			return;
		}

		Engine<Rcs_context> &rcs = gpu->rcs;

		rcs.ppgtt_scratch.check_dump(_env);

		_mmio->flush_gfx_tlb();

		/*
		 * XXX check if HWSP is shared across contexts and if not when
		 *     we actually need to write the register
		 */
		Mmio::HWS_PGA_RCSUNIT::access_t const addr = rcs.hw_status_page();
		_mmio->write_post<Igd::Mmio::HWS_PGA_RCSUNIT>(addr);

		_submit_execlist(rcs);

		_active_vgpu = gpu;
		_timer.trigger_once(WATCHDOG_TIMEOUT);
	}

	/**********
	 ** INTR **
	 **********/

	void _clear_rcs_iir(Mmio::GT_0_INTERRUPT_IIR::access_t const v)
	{
		_mmio->write_post<Mmio::GT_0_INTERRUPT_IIR>(v);
	}

	Vgpu *_last_scheduled = nullptr;

	void _notify_complete(Vgpu *gpu)
	{
		if (!gpu) { return; }

		uint64_t const curr_seqno = gpu->current_seqno();
		uint64_t const comp_seqno = gpu->complete_seqno();

		if (curr_seqno != comp_seqno) {
			Genode::error(__func__, "sequence numbers (", curr_seqno, "/", comp_seqno, ") do not match");
			_last_scheduled = gpu;
			return;
		}

		Execlist &el = *gpu->rcs.execlist;
		el.ring_update_head(gpu->rcs.context->head_offset());

		Genode::Signal_transmitter(gpu->completion_sigh()).submit();
	}

	void _handle_irq()
	{
		_mmio->disable_master_irq();

		Mmio::GT_0_INTERRUPT_IIR::access_t const v = _mmio->read<Mmio::GT_0_INTERRUPT_IIR>();

		bool const ctx_switch    = Mmio::GT_0_INTERRUPT_IIR::Cs_ctx_switch_interrupt::get(v);
		(void)ctx_switch;
		bool const user_complete = Mmio::GT_0_INTERRUPT_IIR::Cs_mi_user_interrupt::get(v);

		if (v) { _clear_rcs_iir(v); }

		Vgpu *notify_gpu = nullptr;
		if (user_complete) { notify_gpu = _current_vgpu(); }

		bool const fault_valid = _mmio->fault_regs_valid();
		if (fault_valid) { Genode::error("FAULT_REG valid"); }

		_mmio->update_context_status_pointer();

		if (user_complete) {
			_unschedule_current_vgpu();
			_active_vgpu = nullptr;

			if (notify_gpu) { _notify_complete(notify_gpu); }

			/* keep the ball rolling...  */
			if (_current_vgpu()) {
				_schedule_current_vgpu();
			}
		}

		_mmio->enable_master_irq();
		_irq->ack_irq();
	}

	Genode::Signal_handler<Device> _irq_dispatcher {
		_env.ep(), *this, &Device::_handle_irq };

	/************
	 ** FENCES **
	 ************/

	/* TODO introduce Fences object, Bit_allocator */
	enum { INVALID_FENCE = 0xff, };

	uint32_t _get_free_fence()
	{
		return _mmio->find_free_fence();
	}

	uint32_t _update_fence(uint32_t const id,
	                       addr_t   const lower,
	                       addr_t   const upper,
	                       uint32_t const pitch,
	                       bool     const tile_x)
	{
		return _mmio->update_fence(id, lower, upper, pitch, tile_x);
	}

	void _clear_fence(uint32_t const id)
	{
		_mmio->clear_fence(id);
	}

	/**********************
	 ** watchdog timeout **
	 **********************/

	void _handle_watchdog_timeout()
	{
		if (!_active_vgpu) { return; }

		Genode::error("watchdog triggered: engine stuck");
		_mmio->dump();
		_mmio->error_dump();
		_mmio->fault_dump();
		_mmio->execlist_status_dump();
		Vgpu *gpu = _current_vgpu();
		gpu = gpu ? gpu : _last_scheduled;
		gpu->rcs.context->dump();
		gpu->rcs.context->dump_hw_status_page();
		Execlist const &el = *gpu->rcs.execlist;
		el.ring_dump(52);

		_device_reset_and_init();

		if (_active_vgpu == gpu) {
			_unschedule_current_vgpu();
		}

		if (_current_vgpu()) {
			_schedule_current_vgpu();
		}
	}

	Genode::Signal_handler<Device> _watchdog_timeout_sigh {
		_env.ep(), *this, &Device::_handle_watchdog_timeout };

	void _device_reset_and_init()
	{
		_mmio->reset();
		_mmio->clear_errors();
		_mmio->init();
		_mmio->enable_intr();
		_mmio->forcewake_enable();
	}

	/**
	 * Constructor
	 */
	Device(Genode::Env                 &env,
	       Genode::Allocator           &alloc,
	       Platform::Connection        &pci,
	       Platform::Device_capability  cap,
	       Genode::Xml_node             config)
	:
		_env(env), _md_alloc(alloc), _pci(pci), _pci_cap(cap)
	{
		using namespace Genode;

		if (!_supported()) { throw Unsupported_device(); }

		/* trigger device_pd assignment */
		_enable_pci_bus_master();

		/*
		 * IHD-OS-BDW-Vol 2c-11.15 p. 1068
		 */
		struct MGGC_0_2_0_PCI : Genode::Register<16>
		{
			struct Graphics_mode_select               : Bitfield<8, 8> { };
			struct Gtt_graphics_memory_size           : Bitfield<6, 2> { };
			struct Versatile_acceleration_mode_enable : Bitfield<3, 1> { };
			struct Igd_vga_disable                    : Bitfield<2, 1> { };
			struct Ggc_lock                           : Bitfield<0, 1> { };
		};
		enum { PCI_GMCH_CTL = 0x50, };
		MGGC_0_2_0_PCI::access_t v = _config_read<uint16_t>(PCI_GMCH_CTL);

		{
			log("MGGC_0_2_0_PCI");
			log("  Graphics_mode_select:               ", Hex(MGGC_0_2_0_PCI::Graphics_mode_select::get(v)));
			log("  Gtt_graphics_memory_size:           ", Hex(MGGC_0_2_0_PCI::Gtt_graphics_memory_size::get(v)));
			log("  Versatile_acceleration_mode_enable: ", Hex(MGGC_0_2_0_PCI::Versatile_acceleration_mode_enable::get(v)));
			log("  Igd_vga_disable:                    ", Hex(MGGC_0_2_0_PCI::Igd_vga_disable::get(v)));
			log("  Ggc_lock:                           ", Hex(MGGC_0_2_0_PCI::Ggc_lock::get(v)));
		}

		/* map PCI resources */
		_poke_pci_resource(GMADR);

		addr_t gttmmadr_base = _map_pci_resource(GTTMMADR);
		_mmio.construct(_delayer, gttmmadr_base);

		if (true) { /* BROADWELL - bdw_sseu_info_init */
			unsigned const r32        = _mmio->read<Igd::Mmio::FUSE2_GEN8>();
			unsigned const slice_mask = _mmio->read<Igd::Mmio::FUSE2_GEN8::Gt_slice_enable_fuse>();
/*
			uint8_t max_slices = 3;
			uint8_t max_subslices = 3;
			uint8_t max_eus_per_subslice = 8;
*/
			error("slice_mask ", Genode::Hex(slice_mask), " register=", Genode::Hex(r32));
		}

		// drivers/gpu/drm/i915/intel_device_info.c bdw_sseu_info_init
//	fuse2 = I915_READ(GEN8_FUSE2);
//	sseu->slice_mask = (fuse2 & GEN8_F2_S_ENA_MASK) >> GEN8_F2_S_ENA_SHIFT;
		//intel_sseu_set_info(sseu, 3, 3, 8);
/*
void intel_sseu_set_info(struct sseu_dev_info *sseu, u8 max_slices,
			 u8 max_subslices, u8 max_eus_per_subslice)
{
	sseu->max_slices = max_slices;
	sseu->max_subslices = max_subslices;
	sseu->max_eus_per_subslice = max_eus_per_subslice;

	sseu->ss_stride = GEN_SSEU_STRIDE(sseu->max_subslices);
	GEM_BUG_ON(sseu->ss_stride > GEN_MAX_SUBSLICE_STRIDE);
	sseu->eu_stride = GEN_SSEU_STRIDE(sseu->max_eus_per_subslice);
	GEM_BUG_ON(sseu->eu_stride > GEN_MAX_EU_STRIDE);
}
*/
		/* GGTT */
		Number_of_bytes const fb_size =
			config.attribute_value("fb_size", 32u<<20);
		log("Reserve beginning ", fb_size, " in GGTT for framebuffer");

		Ram_dataspace_capability scratch_page_ds = _pci_backend_alloc.alloc(PAGE_SIZE);
		addr_t const scratch_page = Dataspace_client(scratch_page_ds).phys_addr();

		size_t const ggtt_size = (1u << MGGC_0_2_0_PCI::Gtt_graphics_memory_size::get(v)) << 20;
		addr_t const ggtt_base = gttmmadr_base + (_res_size[GTTMMADR] / 2);
		size_t const gmadr_size = _res_size[GMADR];
		_ggtt.construct(*_mmio, ggtt_base, ggtt_size, gmadr_size, scratch_page, fb_size);
		_ggtt->dump();

		_vgpu_avail = (gmadr_size - fb_size) / Vgpu::APERTURE_SIZE;

		_device_reset_and_init();

		_irq.construct(_device.irq(0));
		_irq->sigh(_irq_dispatcher);
		_irq->ack_irq();

		_mmio->dump();
		_mmio->context_status_pointer_dump();

		_timer.sigh(_watchdog_timeout_sigh);
	}

	/*********************
	 ** Device handling **
	 *********************/

	/**
	 * Reset the physical device
	 */
	void reset() { _device_reset_and_init(); }

	/**
	 * Get chip id of the phyiscal device
	 */
	uint16_t id() const { return _info.id; }

	/**
	 * Get features of the phyiscal device
	 */
	uint32_t features() const { return _info.features; }

	/*******************
	 ** Vgpu handling **
	 *******************/

	/**
	 * Add vGPU to scheduling list
	 *
	 * \param vgpu  reference to vGPU
	 *
	 * \throw Already_scheduled
	 */
	void vgpu_enqueue(Vgpu &vgpu)
	{
		if (_vgpu_already_scheduled(vgpu)) { throw Already_scheduled(); }

		Vgpu const *pending = _current_vgpu();

		_vgpu_list.enqueue(vgpu);

		if (pending) { return; }

		/* none pending, kick-off execution */
		_schedule_current_vgpu();
	}

	/**
	 * Check if there is a vGPU slot left
	 *
	 * \return true if slot is free, otherwise false is returned
	 */
	bool vgpu_avail() const { return _vgpu_avail; }

	/**
	 * Increase/decrease available vGPU count.
	 */
	void vgpu_created()  { _vgpu_avail --; }
	void vgpu_released() { _vgpu_avail ++; }

	/**
	 * Check if vGPU is currently scheduled
	 *
	 * \return true if vGPU is scheduled, otherwise false is returned
	 */
	bool vgpu_active(Vgpu const &vgpu) const
	{
		bool result = false;
		_vgpu_list.head([&] (Vgpu const &curr) {
			result = (&vgpu == &curr);
		});
		return result;
	}

	/*********************
	 ** Buffer handling **
	 *********************/

	/**
	 * Allocate DMA buffer
	 *
	 * \param guard  resource allocator and guard
	 * \param size   size of the DMA buffer
	 *
	 * \return DMA buffer capability
	 *
	 * \throw Out_of_memory
	 */
	Genode::Dataspace_capability alloc_buffer(Allocator &,
	                                          size_t const size)
	{
		return _pci_backend_alloc.alloc(size);
	}

	/**
	 * Free DMA buffer
	 *
	 * \param guard  resource allocator and guard
	 * \param cap    DMA buffer capability
	 */
	void free_buffer(Allocator &,
	                 Dataspace_capability const cap)
	{
		if (!cap.valid()) { return; }

		_pci_backend_alloc.free(Genode::static_cap_cast<Genode::Ram_dataspace>(cap));
	}

	/**
	 * Map DMA buffer in the GGTT
	 *
	 * \param guard     resource allocator and guard
	 * \param cap       DMA buffer capability
	 * \param aperture  true if mapping should be accessible by CPU
	 *
	 * \return GGTT mapping
	 *
	 * \throw Could_not_map_buffer
	 */
	Ggtt::Mapping const &map_buffer(Genode::Allocator &guard,
	                                Genode::Dataspace_capability cap, bool aperture)
	{
		size_t const size = Genode::Dataspace_client(cap).size();
		try {
			size_t const num = size / PAGE_SIZE;
			Ggtt::Offset const offset = _ggtt->find_free(num, aperture);
			return map_dataspace_ggtt(guard, cap, offset);
		} catch (...) {
			throw Could_not_map_buffer();
		}
	}

	/**
	 * Unmap DMA buffer from GGTT
	 *
	 * \param guard    resource allocator and guard
	 * \param mapping  GGTT mapping
	 */
	void unmap_buffer(Genode::Allocator &guard, Ggtt::Mapping mapping)
	{
		unmap_dataspace_ggtt(guard, mapping.cap);
	}

	/**
	 * Set tiling mode for GGTT region
	 *
	 * \param start  offset of the GGTT start entry
	 * \param size   size of the region
	 * \param mode   tiling mode for the region
	 *
	 * \return id of the used fence register
	 */
	uint32_t set_tiling(Ggtt::Offset const start, size_t const size,
	                    uint32_t const mode)
	{
		uint32_t const id = _mmio->find_free_fence();
		if (id == INVALID_FENCE) {
			Genode::warning("could not find free FENCE");
			return false;
		}

		addr_t   const lower = start * PAGE_SIZE;
		addr_t   const upper = lower + size;
		uint32_t const pitch = ((mode & 0xffff0000) >> 16) / 128 - 1;
		bool     const tilex = (mode & 0x1);

		return _update_fence(id, lower, upper, pitch, tilex);
	}

	/**
	 * Clear tiling for given fence
	 *
	 * \param id  id of fence register
	 */
	void clear_tiling(uint32_t const id)
	{
		_clear_fence(id);
	}

	private:

		/*
		 * Noncopyable
		 */
		Device(Device const &);
		Device &operator = (Device const &);
};


namespace Gpu {

	class Session_component;
	class Root;

	using Root_component = Genode::Root_component<Session_component, Genode::Multiple_clients>;
}


class Gpu::Session_component : public Genode::Session_object<Gpu::Session>
{
	private:

		Genode::Region_map       &_rm;
		Constrained_ram_allocator _ram;
		Heap                      _heap { _ram, _rm };

		Igd::Device       &_device;
		Igd::Device::Vgpu  _vgpu;

		struct Buffer
		{
			Genode::Dataspace_capability cap;

			Gpu::addr_t ppgtt_va       { };
			bool        ppgtt_va_valid { false };

			enum { INVALID_FENCE = 0xff };
			Genode::uint32_t fenced { INVALID_FENCE };

			Igd::Ggtt::Mapping map { };

			Buffer(Genode::Dataspace_capability cap) : cap(cap) { }

			virtual ~Buffer() { }
		};

		Genode::Registry<Genode::Registered<Buffer>> _buffer_registry { };

		Genode::uint64_t seqno { 0 };

		void _free_buffers()
		{
			auto lookup_and_free = [&] (Buffer &buffer) {

				if (buffer.map.offset != Igd::Ggtt::Mapping::INVALID_OFFSET) {
					_device.unmap_buffer(_heap, buffer.map);
				}

				if (buffer.fenced != Buffer::INVALID_FENCE) {
					_device.clear_tiling(buffer.fenced);
					_vgpu.active_fences--;
				}

				Genode::Dataspace_client buf(buffer.cap);
				Genode::size_t const actual_size = buf.size();
				_vgpu.rcs_unmap_ppgtt(buffer.ppgtt_va, actual_size);

				_device.free_buffer(_heap, buffer.cap);
				Genode::destroy(&_heap, &buffer);
			};
			_buffer_registry.for_each(lookup_and_free);
		}

	public:

		/**
		 * Constructor
		 *
		 * \param rm         region map used for attaching GPU resources
		 * \param md_alloc   meta-data allocator
		 * \param ram_quota  initial ram quota
		 * \param device     reference to the physical device
		 */
		Session_component(Entrypoint    &ep,
		                  Ram_allocator &ram,
		                  Region_map    &rm,
		                  Resources      resources,
		                  Label   const &label,
		                  Diag           diag,
		                  Igd::Device   &device)
		:
			Session_object(ep, resources, label, diag),
			_rm(rm),
			_ram(ram, _ram_quota_guard(), _cap_quota_guard()),
			_device(device),
			_vgpu(_device, _heap)
		{ }

		~Session_component()
		{
			_free_buffers();
		}

		/*********************************
		 ** Session_component interface **
		 *********************************/

		bool vgpu_active() const
		{
			return _device.vgpu_active(_vgpu);
		}

		/***************************
		 ** Gpu session interface **
		 ***************************/

		Info info() const override
		{
			Genode::size_t const aperture_size = Igd::Device::Vgpu::APERTURE_SIZE;
			return Info(_device.id(), _device.features(), aperture_size,
			            _vgpu.id(), { .id = _vgpu.complete_seqno() });
		}

		Gpu::Info::Execution_buffer_sequence exec_buffer(Genode::Dataspace_capability cap,
		                                                 Genode::size_t) override
		{
			bool found = false;

			_buffer_registry.for_each([&] (Buffer &buffer) {
				if (!(buffer.cap == cap)) { return; }

				if (!buffer.ppgtt_va_valid) {
					Genode::error("Invalid execbuffer");
					Genode::Signal_transmitter(_vgpu.completion_sigh()).submit();
					throw Gpu::Session::Invalid_state();
				}

				_vgpu.setup_ring_buffer(buffer.ppgtt_va, _device._ggtt->scratch_page());
				found = true;
			});

			if (!found)
				throw Gpu::Session::Invalid_state();

			try {
				_device.vgpu_enqueue(_vgpu);
				return { .id = _vgpu.current_seqno() };
			} catch (Igd::Device::Already_scheduled &e) {
				Genode::error("vGPU already scheduled");
				return { .id = _vgpu.current_seqno() }; /* XXX */
			}
		}

		void completion_sigh(Genode::Signal_context_capability sigh) override
		{
			_vgpu.completion_sigh(sigh);
		}

		Genode::Dataspace_capability alloc_buffer(Genode::size_t size) override
		{
			/*
			 * XXX allocator overhead is not
			 *     included, mapping costs are not included and we throw at
			 *     different locations...
			 *
			 *     => better construct Buffer object as whole
			 */

			/* roundup to next page size and add guarding page */
			size = ((size + 0xffful) & ~0xffful); // + 0x1000;

			try {
				Genode::Dataspace_capability cap = _device.alloc_buffer(_heap, size);

				try {
					new (&_heap) Genode::Registered<Buffer>(_buffer_registry, cap);
				} catch (...) {
					if (cap.valid())
						_device.free_buffer(_heap, cap);
					throw Gpu::Session_component::Out_of_ram();
				}
				return cap;
			} catch (Igd::Device::Out_of_ram) {
				throw Gpu::Session_component::Out_of_ram();
			}

			return Genode::Dataspace_capability();
		}

		void free_buffer(Genode::Dataspace_capability cap) override
		{
			if (!cap.valid()) { return; }

			auto lookup_and_free = [&] (Buffer &buffer) {
				if (!(buffer.cap == cap)) { return; }

				if (buffer.map.offset != Igd::Ggtt::Mapping::INVALID_OFFSET) {
					Genode::error("cannot free mapped buffer");
					/* XXX throw */
				}

				_device.free_buffer(_heap, cap);
				Genode::destroy(&_heap, &buffer);
			};
			_buffer_registry.for_each(lookup_and_free);
		}

		Genode::Dataspace_capability map_buffer(Genode::Dataspace_capability cap,
		                                        bool aperture) override
		{
			if (!cap.valid()) { return Genode::Dataspace_capability(); }

			Genode::Dataspace_capability map_cap;

			auto lookup_and_map = [&] (Buffer &buffer) {
				if (!(buffer.cap == cap)) { return; }

				if (buffer.map.offset != Igd::Ggtt::Mapping::INVALID_OFFSET) {
					Genode::error("buffer already mapped");
					return;
				}

				try {
					Igd::Ggtt::Mapping const &map = _device.map_buffer(_heap, cap, aperture);
					buffer.map.cap    = map.cap;
					buffer.map.offset = map.offset;
					map_cap           = buffer.map.cap;
				} catch (Igd::Device::Could_not_map_buffer) {
					Genode::error("could not map buffer object");
					throw Gpu::Session::Out_of_ram();
				}
			};
			_buffer_registry.for_each(lookup_and_map);

			return map_cap;
		}

		void unmap_buffer(Genode::Dataspace_capability cap) override
		{
			if (!cap.valid()) { return; }

			bool unmapped = false;

			auto lookup_and_unmap = [&] (Buffer &buffer) {
				if (!(buffer.map.cap == cap)) { return; }

				if (buffer.fenced != Buffer::INVALID_FENCE) {
					_device.clear_tiling(buffer.fenced);
					_vgpu.active_fences--;
				}

				_device.unmap_buffer(_heap, buffer.map);
				buffer.map.offset = Igd::Ggtt::Mapping::INVALID_OFFSET;
				unmapped = true;
			};
			_buffer_registry.for_each(lookup_and_unmap);

			if (!unmapped) { Genode::error("buffer not mapped"); }
		}

		bool map_buffer_ppgtt(Genode::Dataspace_capability cap,
		                      Gpu::addr_t va) override
		{
			if (!cap.valid()) { return false; }

			enum { ALLOC_FAILED, MAP_FAILED, OK } result = ALLOC_FAILED;

			auto lookup_and_map = [&] (Buffer &buffer) {
				if (!(buffer.cap == cap)) { return; }

				if (buffer.ppgtt_va_valid) {
					Genode::error("buffer already mapped");
					return;
				}

				try {
					Genode::Dataspace_client buf(cap);
					/* XXX check that actual_size matches alloc_buffer size */
					Genode::size_t const actual_size = buf.size();
					Genode::addr_t const phys_addr   = buf.phys_addr();
					_vgpu.rcs_map_ppgtt(va, phys_addr, actual_size);
					buffer.ppgtt_va = va;
					buffer.ppgtt_va_valid = true;
					result = OK;
				} catch (Igd::Device::Could_not_map_buffer) {
					Genode::error("could not map buffer object (", Genode::Hex(va), ") into PPGTT");
					result = MAP_FAILED;
					return;
				}
				catch (Igd::Device::Out_of_ram) {
					result = ALLOC_FAILED;
					return;
				}
			};
			_buffer_registry.for_each(lookup_and_map);

			switch (result) {
			case ALLOC_FAILED: throw Gpu::Session::Out_of_ram();
			case MAP_FAILED:   throw Gpu::Session::Mapping_buffer_failed();
			case OK: return true;
			default:
				return false;
			}
		}

		void unmap_buffer_ppgtt(Genode::Dataspace_capability cap,
		                        Gpu::addr_t va) override
		{
			if (!cap.valid()) {
				Genode::error("invalid buffer capability");
				return;
			}

			auto lookup_and_unmap = [&] (Buffer &buffer) {
				if (!(buffer.cap == cap)) { return; }

				if (!buffer.ppgtt_va_valid) {
					Genode::error("buffer not mapped");
					return;
				}

				if (buffer.ppgtt_va != va) {
					Genode::error("buffer not mapped at ", Genode::Hex(va));
					return;
				}

				Genode::Dataspace_client buf(cap);
				Genode::size_t const actual_size = buf.size();
				_vgpu.rcs_unmap_ppgtt(va, actual_size);
				buffer.ppgtt_va_valid = false;
			};
			_buffer_registry.for_each(lookup_and_unmap);
		}

		bool set_tiling(Genode::Dataspace_capability cap,
		                Genode::uint32_t const mode) override
		{
			if (_vgpu.active_fences > Igd::Device::Vgpu::MAX_FENCES) {
				Genode::error("no free fences left, already active: ", _vgpu.active_fences);
				return false;
			}

			Buffer *b = nullptr;
			auto lookup = [&] (Buffer &buffer) {
				if (!(buffer.map.cap == cap)) { return; }
				b = &buffer;
			};
			_buffer_registry.for_each(lookup);

			if (b == nullptr) {
				Genode::error("attempt to set tiling for non-mapped buffer");
				return false;
			}

			Igd::size_t const size = Genode::Dataspace_client(b->cap).size();
			Genode::uint32_t const fenced = _device.set_tiling(b->map.offset, size, mode);
			b->fenced = fenced;
			if (fenced != Buffer::INVALID_FENCE) { _vgpu.active_fences++; }
			return fenced;
		}
};


class Gpu::Root : public Gpu::Root_component
{
	private:

		/*
		 * Noncopyable
		 */
		Root(Root const &);
		Root &operator = (Root const &);

		Genode::Env &_env;
		Igd::Device *_device;

		Genode::size_t _ram_quota(char const *args)
		{
			return Genode::Arg_string::find_arg(args, "ram_quota").ulong_value(0);
		}

	protected:

		Session_component *_create_session(char const *args) override
		{
			if (!_device || !_device->vgpu_avail()) {
				throw Genode::Service_denied();
			}

			/* at the moment we just need about ~160KiB for initial RCS bring-up */
			Genode::size_t const required_quota = Gpu::Session::REQUIRED_QUOTA / 2;
			Genode::size_t const ram_quota      = _ram_quota(args);

			if (ram_quota < required_quota) {
				Genode::Session_label const label = Genode::label_from_args(args);
				Genode::warning("insufficient dontated ram_quota (", ram_quota,
				                " bytes), require ", required_quota, " bytes ",
				                " by '", label, "'");
				throw Session::Out_of_ram();
			}

			try {
				using namespace Genode;

				return new (md_alloc())
					Session_component(_env.ep(), _env.ram(), _env.rm(),
					                  session_resources_from_args(args),
					                  session_label_from_args(args),
					                  session_diag_from_args(args),
					                  *_device);
			} catch (...) { throw; }
		}

		void _upgrade_session(Session_component *s, const char *args) override
		{
			s->upgrade(ram_quota_from_args(args));
			s->upgrade(cap_quota_from_args(args));
		}

		void _destroy_session(Session_component *s) override
		{
			if (s->vgpu_active()) {
				Genode::warning("vGPU active, reset device and hope for the best");
				_device->reset();
			}
			Genode::destroy(md_alloc(), s);
		}

	public:

		Root(Genode::Env &env, Genode::Allocator &alloc)
		: Root_component(env.ep(), alloc), _env(env), _device(nullptr) { }

		void manage(Igd::Device &device) { _device = &device; }
};


struct Main
{
	Genode::Env &_env;

	/*********
	 ** Pci **
	 *********/

	Platform::Connection         _pci;
	Platform::Device_capability  _pci_cap { };

	Platform::Device_capability _find_gpu_device()
	{
		using namespace Platform;

		auto _scan_pci = [&] (Platform::Connection    &pci,
		                      Device_capability const &prev) {
			Device_capability cap = Genode::retry<Platform::Out_of_ram>(
				[&] () { return pci.next_device(prev, 0, 0); },
				[&] () { pci.upgrade_ram(4096); }, 8);

			if (prev.valid()) { pci.release_device(prev); }
			return cap;
		};

		Device_capability cap;

		while ((cap = _scan_pci(_pci, cap)).valid()) {
			Device_client device(cap);

			enum { BDW_DEVICE_ID = 0x1600, };
			if ((device.class_code() >> 8) == 0x0300
			    && (device.device_id() & 0xff00) == BDW_DEVICE_ID) {
				return cap;
			}
		}

		return Device_capability();
	}

	Platform::Device_capability _find_bridge()
	{
		using namespace Platform;

		auto _scan_pci = [&] (Platform::Connection    &pci,
		                      Device_capability const &prev) {
			Device_capability cap;

			cap = Genode::retry<Platform::Out_of_ram>(
				[&] () { return pci.next_device(prev, 0, 0); },
				[&] () { pci.upgrade_ram(4096); }, 8);

			if (prev.valid()) { pci.release_device(prev); }
			return cap;
		};

		Device_capability cap;

		unsigned char bus = 0xff, dev = 0xff, func = 0xff;
		while ((cap = _scan_pci(_pci, cap)).valid()) {
			Device_client device(cap);

			device.bus_address(&bus, &dev, &func);

			if (bus == 0 && dev == 0 && func == 0) {
				return cap;
			}
		}

		return Device_capability();
	}

	bool _mch_enabled()
	{
		using namespace Platform;

		Device_capability cap = _find_bridge();
		if (!cap.valid()) { return false; }

		Device_client device(cap);

		/*
		 * 5th Gen Core Processor datasheet vol 2 p. 48
		 */
		enum { MCHBAR_OFFSET = 0x48, };
		struct MCHBAR : Genode::Register<64>
		{
			struct Mchbaren : Bitfield<0, 1> { };
		};

		MCHBAR::access_t const v = device.config_read(MCHBAR_OFFSET,
		                                              Device::ACCESS_32BIT);
		return MCHBAR::Mchbaren::get(v);
	}

	/*********
	 ** Gpu **
	 *********/

	Genode::Sliced_heap _root_heap { _env.ram(), _env.rm() };
	Gpu::Root           _gpu_root  { _env, _root_heap };

	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	Genode::Heap                       _device_md_alloc;
	Genode::Constructible<Igd::Device> _device { };

	Main(Genode::Env &env)
	:
		_env(env), _pci(env), _device_md_alloc(_env.ram(), _env.rm())
	{
		/* initial donation for device pd */
		_pci.upgrade_ram(1024*1024);

		_pci_cap = _find_gpu_device();
		if (!_pci_cap.valid() || !_mch_enabled()) {
			throw Igd::Device::Initialization_failed();
		}

		try {
			_device.construct(_env, _device_md_alloc, _pci, _pci_cap,
			                  _config_rom.xml());
		} catch (...) {
			_env.parent().exit(1);
			return;
		}

		_gpu_root.manage(*_device);
		_env.parent().announce(_env.ep().manage(_gpu_root));
	}

	~Main() { _pci.release_device(_pci_cap); }
};


void Component::construct(Genode::Env &env) { static Main main(env); }


Genode::size_t Component::stack_size() { return 32UL*1024*sizeof(long); }
