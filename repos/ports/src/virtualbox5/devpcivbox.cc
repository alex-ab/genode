/*
 * \brief  VBox PCI device model
 * \author Josef Soentgen
 * \date   2021-01-07
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

/* Genode includes */
#include <base/log.h>
#include <base/attached_rom_dataspace.h>
#include <util/list.h>
#include <timer_session/connection.h>
#include <util/mmio.h>

/* libc internal includes */
#include <internal/thread_create.h>

/* Virtualbox includes */
#include <VBox/pci.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/mm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>
#ifdef IN_RING3
# include <iprt/alloca.h>
# include <iprt/mem.h>
# include <iprt/thread.h>
# include <iprt/uuid.h>
#endif
#include <VBox/vusb.h>
#include <VBoxDD.h>

/* VBox Genode specific */
#include "vmm.h"


#include <trace/tracer.h>

static Tracer::Id _tracer_id[16];
static uint32_t   _max_tracer_id;
static uint64_t   _last_vm_exits;

extern "C" uint64_t _last_tsc_run_hw;
extern "C" uint64_t _last_tsc;
extern "C" uint64_t _lapic_exit;
extern "C" uint64_t _ioapic_exit;
extern "C" uint64_t _ioinstr_exit;
extern "C" uint64_t _vm_exits;
extern "C" uint64_t _vm_exit_irq_win;

uint64_t _last_tsc = 0;
uint64_t _last_tsc_run_hw = 0;
uint64_t _lapic_exit = 0;
uint64_t _ioapic_exit = 0;
uint64_t _ioinstr_exit = 0;
uint64_t _vm_exits = 0;
uint64_t _vm_exit_irq_win = 0;


#define ENABLE_TRACING 1

#if ENABLE_TRACING
#define TRACE(...) do { Genode::trace(Genode::Thread::myself()->name(), ": ", __VA_ARGS__); } while (0)
#else
#define TRACE(...)
#endif


/************************
 ** xHCI device struct **
 ************************/

namespace {
struct Tq;
}
struct Controller;


struct PCIVBOX
{
	PDMPCIDEV    PciDev;
	PPDMDEVINSR3 pDevInsR3;
	PPDMDEVINSR0 pDevInsR0;
	PPDMDEVINSRC pDevInsRC;
	RTGCPHYS32   MMIOBase;

	PTMTIMERR3   controller_timer;
	Tq *timer_queue;
	Controller  *controller;

	Genode::Entrypoint *pci_ep;
};


typedef struct PCIVBOX *PPCIVBOX;
typedef struct PCIVBOX const *PCPCIVBOX;


/*************************************
 ** ::Controller helper classes **
 *************************************/

static bool const verbose_timer = true;

namespace {

struct Tq
{
	struct Context : public Genode::List<Context>::Element
	{
		uint64_t timeout_abs_ns = ~0ULL;
		bool     pending        = false;

		void  *qtimer     = nullptr;
		void (*cb)(void*) = nullptr;
		void  *data       = nullptr;

		Context(void *qtimer, void (*cb)(void*), void *data)
		: qtimer(qtimer), cb(cb), data(data) { }
	};

	Genode::List<Context> _context_list;
	PTMTIMER              tm_timer;

	void _append_new_context(void *qtimer, void (*cb)(void*), void *data)
	{
		Context *new_ctx = new (vmm_heap()) Context(qtimer, cb, data);
		_context_list.insert(new_ctx);
	}

	Context *_find_context(void const *qtimer)
	{
		for (Context *c = _context_list.first(); c; c = c->next())
			if (c->qtimer == qtimer)
				return c;
		return nullptr;
	}

	Context *_min_pending()
	{
		Context *min = nullptr;
		for (min = _context_list.first(); min; min = min->next())
			if (min && min->pending)
				break;

		if (!min || !min->next())
			return min;

		for (Context *c = min->next(); c; c = c->next()) {
			if ((c->timeout_abs_ns < min->timeout_abs_ns) && c->pending)
				min = c;
		}

		return min;
	}

	void _program_min_timer()
	{
		Context *min = _min_pending();
		if (min == nullptr) return;

		if (TMTimerIsActive(tm_timer))
			TMTimerStop(tm_timer);

		uint64_t now = TMTimerGetNano(tm_timer);
		TMTimerSetNano(tm_timer, min->timeout_abs_ns - now);
	}

	void _deactivate_timer(void *qtimer)
	{
		Context *c = _find_context(qtimer);
		if (c == nullptr) {
			Genode::error("qtimer: ", qtimer, " not found");
			throw -1;
		}

		if (c == _min_pending()) {
			c->pending = false;
			TMTimerStop(tm_timer);
			_program_min_timer();
		}

		c->pending = false;
	}

	Tq(PTMTIMER timer) : tm_timer(timer) { }

	void timeout()
	{
		uint64_t now = TMTimerGetNano(tm_timer);

		for (Context *c = _context_list.first(); c; c = c->next()) {
			if (c->pending && c->timeout_abs_ns <= now) {
				c->pending = false;
				// ::usb_timer_callback(c->cb, c->data);
				c->cb(c->data);
			}
		}

		_program_min_timer();
	}

	/**********************
	 ** TMTimer callback **
	 **********************/

	static DECLCALLBACK(void) tm_timer_cb(PPDMDEVINS pDevIns, PTMTIMER pTimer, void *pvUser)
	{
		PPCIVBOX pThis    = PDMINS_2_DATA(pDevIns, PPCIVBOX);
		Tq *q = pThis->timer_queue;

		q->timeout();
	}


	unsigned count_timer()
	{
		unsigned res = 0;

		for (Context *c = _context_list.first(); c; c = c->next()) {
			if (c->pending) Genode::log("timer: ", c, " is pending");
			res++;
		}

		return res;
	}

	/*********************************

	 ** ::Tq interface **
	 *********************************/

	::int64_t get_ns()
	{
		uint64_t const now = TMTimerGetNano(tm_timer);
		return now;
	}

	Genode::Mutex _timer_mutex { };

	void register_timer(void *qtimer, void (*cb)(void*), void *data)
	{
		Genode::Mutex::Guard guard(_timer_mutex);

		Context *c = _find_context(qtimer);
		if (c != nullptr) {
			Genode::error("qtimer: ", qtimer, " already registred");
			throw -1;
		}

		_append_new_context(qtimer, cb, data);
	}

	void delete_timer(void *qtimer) 
	{
		Genode::Mutex::Guard guard(_timer_mutex);

		Context *c = _find_context(qtimer);
		if (c == nullptr) {
			Genode::error("qtimer: ", qtimer, " not found");
			throw -1;
		}

		_deactivate_timer(qtimer);

		_context_list.remove(c);
		Genode::destroy(vmm_heap(), c);
	}

	void activate_timer(void *qtimer, long long int expire_abs) 
	{
		Genode::Mutex::Guard guard(_timer_mutex);

		Context *c = _find_context(qtimer);
		if (c == nullptr) {
			Genode::error("qtimer: ", qtimer, " not found");
			throw -1;
		}

		c->timeout_abs_ns = expire_abs;
		c->pending        = true;

		_program_min_timer();
	}

	void deactivate_timer(void *qtimer) 
	{
		Genode::Mutex::Guard guard(_timer_mutex);

		_deactivate_timer(qtimer);
	}
};

} /* namespace anonymous */


struct Pci_device
{
	PPDMDEVINS pci_dev;

	Pci_device(PPDMDEVINS pDevIns) : pci_dev(pDevIns) { }

	void raise_interrupt(int level) {
		PDMDevHlpPCISetIrq(pci_dev, 0, level); }

	int read_dma(Genode::addr_t addr, void *buf, Genode::size_t size) {
		return PDMDevHlpPhysRead(pci_dev, addr, buf, size); }

	int write_dma(Genode::addr_t addr, void const *buf, Genode::size_t size) {
		return PDMDevHlpPhysWrite(pci_dev, addr, buf, size); }

	void *map_dma(Genode::addr_t base, Genode::size_t size)
	{
		PGMPAGEMAPLOCK lock;
		void * vmm_addr = nullptr;

		int rc = PDMDevHlpPhysGCPhys2CCPtr(pci_dev, base, 0, &vmm_addr, &lock);
		Assert(rc == VINF_SUCCESS);

		/* the mapping doesn't go away, so release internal lock immediately */
		PDMDevHlpPhysReleasePageMappingLock(pci_dev, &lock);
		return vmm_addr;
	}

	void unmap_dma(void *addr, Genode::size_t size) { }
};


static void handle_timer(void *);


struct Controller
{
	enum { MMIO_SIZE = 4096, };
	char _mmio_space[MMIO_SIZE] { };

	struct Mmio : public Genode::Mmio
	{
		struct Config : Register<0x04, 32>
		{
			struct Enable   : Bitfield< 0,  1> { };
			struct Interval : Bitfield< 1, 31> { };
		};

		struct Status : Register<0x08, 64>
		{
			struct Ready               : Bitfield< 0,  1> { };
			struct Error               : Bitfield< 1,  1> { };
			struct Count               : Bitfield< 2, 16> { };
			struct Interrupt_pending   : Bitfield<18,  1> { };
			struct Interrupt_timestamp : Bitfield<31, 32> { };
		};

		Mmio(Genode::addr_t const base)
		: Genode::Mmio(base) { }
	};

	Mmio _mmio { (Genode::addr_t)_mmio_space };

	Genode::Entrypoint &_ep;
	Pci_device &_pci_dev;

	Tq &_timer_queue;

	inline uint64_t _rdtsc() const
	{
		uint32_t lo, hi;
		__asm__ __volatile__ (
		 "xorl %%eax,%%eax\n\t"
		 "cpuid\n\t"
		 :
		 :
		 : "%rax", "%rbx", "%rcx", "%rdx"
		);
		__asm__ __volatile__ (
		"rdtsc" : "=a" (lo), "=d" (hi)
		);

		return (uint64_t)hi << 32 | lo;
	}

	bool _timeout_fw { false };

	Genode::Constructible<Timer::One_shot_timeout<Controller>> _timer_one_shot { };
	Genode::Constructible<Timer::Connection> _timer { };

	Controller(Genode::Entrypoint &ep, Pci_device &pci_dev, Tq &timer_queue, bool to_fw)
	: _ep { ep }, _pci_dev { pci_dev }, _timer_queue { timer_queue }, _timeout_fw { to_fw }
	{
		try {
			Genode::Attached_rom_dataspace info { genode_env(), "platform_info"};

			Genode::uint64_t result = info.xml().sub_node("hardware")
			                          .sub_node("tsc")
			                          .attribute_value("freq_khz", 0ULL);
			_freq_mhz = result / 1000;
		} catch (...) { }

		Genode::log("tsc frequency: ", _freq_mhz, "MHz");

		if (to_fw) {
			_timer.construct(genode_env(), _ep);
			_timer_one_shot.construct(*_timer, *this, &Controller::_handle_timeout);
		} else {
			_timer_queue.register_timer(this, handle_timer, this);
		}
	}

	void _handle_interval_timeout()
	{
		bool     const enabled  = _mmio.read<Mmio::Config::Enable>();
		uint32_t const interval = _mmio.read<Mmio::Config::Interval>();

		if (enabled && interval > 0) {
			_timer_one_shot->schedule(Genode::Microseconds { interval });
		} else {
			_timer_one_shot->discard();
		}
	}

	void _handle_interval_tm()
	{
		bool     const enabled  = _mmio.read<Mmio::Config::Enable>();
		uint32_t const interval = _mmio.read<Mmio::Config::Interval>();

		if (enabled && interval > 0) {
			int64_t const ns = _timer_queue.get_ns();
			int64_t const new_to = ns + (interval * 1000);
//			TRACE(__func__, ": intr: ", _interrupts, " ", ns, " interval=", interval, "*1000 -> new_to=", new_to);
			_timer_queue.activate_timer(this, new_to);
		} else {
			_timer_queue.deactivate_timer(this);
		}
	}

	uint64_t _interrupts { 0 };
	uint64_t _freq_mhz { 2100 };

	void _interrupt()
	{
		stop_measurement(__func__);

		uint64_t const tsc = _rdtsc();
		uint64_t const diff = (tsc - _last_tsc) / _freq_mhz;
		_last_tsc = tsc;
		_last_tsc_run_hw = 1;
		_last_vm_exits = _vm_exits;

		++_interrupts;
		// Genode::error(__func__, ": intr: ", _interrupts,  " diff: ", diff, " us");
		TRACE(__func__, ": intr: ", _interrupts,  " diff: ", diff, " us");

		uint32_t const its = (tsc / _freq_mhz) & 0xffffffff;
		_mmio.write<Mmio::Status::Interrupt_timestamp>(its);

		uint16_t const cnt = _mmio.read<Mmio::Status::Count>();
		_mmio.write<Mmio::Status::Count>(cnt+1);

		_mmio.write<Mmio::Status::Interrupt_pending>(1);
		_pci_dev.raise_interrupt(1);

#if ENABLE_TRACING
		if (_interrupts % 5000 == 0) {
			for (uint32_t i = 0; i < _max_tracer_id; i++) {
				// Genode::log("DUMPSTART ", _tracer_id[i].value, " ", count);
				Tracer::dump_trace_buffer(_tracer_id[i]);
				// Genode::log("DUMPEND ", _tracer_id[i].value, " ", count);
			}
		}
#endif
	}

	void _handle_timeout(Genode::Duration)
	{
		_interrupt();
		_handle_interval_timeout();
	}

	void interrupt()
	{
		_interrupt();
		_handle_interval_tm();
	}

	void mmio_read(off_t offset, void *buf, size_t size)
	{
		uint64_t v = 0;

		switch (offset) {
		case 0x04:
			v = _mmio.read<Mmio::Config>();
			break;
		case 0x08:
			v = _mmio.read<Mmio::Status>();
			break;
		default:
			break;
		}

		switch (size) {
		case 4:
			*(uint32_t*)buf = (uint32_t)v;
			break;
		case 8:
			*(uint64_t*)buf = (uint64_t)v;
			break;
		default:
			break;
		}
	}

	void mmio_write(off_t offset, void const *buf, size_t size)
	{
		uint64_t v = 0;

		switch (size) {
		case 4:
			v = *(uint32_t*)buf;
			break;
		case 8:
			v = *(uint64_t*)buf;
			break;
		default:
			break;
		}

		// Genode::error("offset: ", offset, " v: ", Genode::Hex(v), " size: ", size);

		switch (offset) {
		case 0x04:
		{
			_mmio.write<Mmio::Config>(v);
			if (_mmio.read<Mmio::Config::Enable>()) {
				_mmio.write<Mmio::Status::Ready>(1);
			}

			TRACE(__func__, ": not called ?");

			if (_timeout_fw) {
				_handle_interval_timeout();
			} else {
				_handle_interval_tm();
			}
			break;
		}
		case 0x08:
		{
			if (v & (1u << 18)) {
				_mmio.write<Mmio::Status::Interrupt_pending>(0);
				_pci_dev.raise_interrupt(0);

//				stop_measurement(__func__);
			}
			// _mmio.write<Mmio::Status>(v);
			break;
		}
		default:
			break;
		}
	}

	size_t mmio_size() const { return MMIO_SIZE; }

	void stop_measurement(char const * const func)
	{
		uint64_t const tsc = _rdtsc();
		uint64_t const diff = (tsc - _last_tsc) / _freq_mhz;
		TRACE(func, ": intr: ", _interrupts, " diff: ", diff,
		      " us _run_hw=", _last_tsc_run_hw,
		      " vm_exits=", _vm_exits - _last_vm_exits,
		      " (lapic=", _lapic_exit,
		      " ioapic=", _ioapic_exit,
		      " io=", _ioinstr_exit,
		      " irq_win=", _vm_exit_irq_win,
		      ")");

		_last_tsc_run_hw = 0;
		_lapic_exit = 0;
		_ioapic_exit = 0;
		_ioinstr_exit = 0;
		_vm_exit_irq_win = 0;
	}
};


static void handle_timer(void *myself)
{
	Controller *controller = (Controller*)myself;
	controller->interrupt();
}


/***********************************************
 ** Virtualbox Device function implementation **
 ***********************************************/

/**
 * @callback_method_impl{FNIOMMMIOREAD}
 */
PDMBOTHCBDECL(int) pcivboxMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr,
                                void *pv, unsigned cb)
{
	PPCIVBOX pThis = PDMINS_2_DATA(pDevIns, PPCIVBOX);

	Genode::off_t offset = GCPhysAddr - pThis->MMIOBase;
	Controller *controller = pThis->controller;

	controller->mmio_read(offset, pv, cb);
	return 0;
}


/**
 * @callback_method_impl{FNIOMMMIOWRITE}
 */
PDMBOTHCBDECL(int) pcivboxMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr,
                                 void const *pv, unsigned cb)
{
	PPCIVBOX pThis = PDMINS_2_DATA(pDevIns, PPCIVBOX);

	Genode::off_t const offset     = GCPhysAddr - pThis->MMIOBase;
	Controller *  const controller = pThis->controller;

	controller->mmio_write(offset, pv, cb);
	return 0;
}


/**
 * @callback_method_impl{FNPCIIOREGIONMAP}
 */
static DECLCALLBACK(int) pcivboxR3Map(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                   uint32_t iRegion, RTGCPHYS GCPhysAddress,
                                   RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
	PPCIVBOX pThis = (PPCIVBOX)pPciDev;
	int rc = PDMDevHlpMMIORegister(pThis->CTX_SUFF(pDevIns), GCPhysAddress, cb, NULL /*pvUser*/,
	                               IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED,
	                               pcivboxMmioWrite, pcivboxMmioRead, "PCI VBOX");
	if (RT_FAILURE(rc))
		return rc;

	rc = PDMDevHlpMMIORegisterRC(pDevIns, GCPhysAddress, cb, NIL_RTRCPTR /*pvUser*/,
	                             "pcivboxMmioWrite", "pcivboxMmioRead");
	if (RT_FAILURE(rc))
		return rc;

	pThis->MMIOBase = GCPhysAddress;
	return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) pcivboxReset(PPDMDEVINS pDevIns)
{
	PPCIVBOX pThis = PDMINS_2_DATA(pDevIns, PPCIVBOX);
	/* todo */
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) pcivboxDestruct(PPDMDEVINS pDevIns)
{
	return 0;
}


struct Pci_ep : Genode::Entrypoint
{
	pthread_t _pthread;

	void _handle_pthread_registration()
	{
		Genode::Thread *myself = Genode::Thread::myself();
		if (!myself || Libc::pthread_create(&_pthread, *myself, &myself)) {
			Genode::error("PCI VBOX will not work - thread for "
			              "pthread registration invalid");
		}
	}

	Genode::Signal_handler<Pci_ep> _pthread_reg_sigh;

	enum { USB_EP_STACK = 32u << 10, };

	Pci_ep(Genode::Env &env)
	:
		Entrypoint(env, USB_EP_STACK, "pci_ep", Genode::Affinity::Location()),
		_pthread_reg_sigh(*this, *this, &Pci_ep::_handle_pthread_registration)
	{
		Genode::Signal_transmitter(_pthread_reg_sigh).submit();
	}
};


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct,XHCI constructor}
 */
static DECLCALLBACK(int) pcivboxR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
	PPCIVBOX pThis = PDMINS_2_DATA(pDevIns, PPCIVBOX);
	PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);

	/**
	 * Use TMCLOCK_VIRTUAL_SYNC which looks worse than TMCLOCK_VIRTUAL_SYNC
	 * but “sounds” better but I don't know why…
	 */
	int rc = PDMDevHlpTMTimerCreate(pDevIns, TMCLOCK_VIRTUAL, Tq::tm_timer_cb,
	                                pThis, TMTIMER_FLAGS_NO_CRIT_SECT,
	                                "PCI Timer", &pThis->controller_timer);
	if (rc) {
		Genode::error("could not create timer");
		throw -1;
	}
	static Tq timer_queue(pThis->controller_timer);
	pThis->timer_queue = &timer_queue;
	static Pci_device pci_device(pDevIns);

	pThis->pci_ep = new Pci_ep(genode_env());

#if ENABLE_TRACING
	Tracer::Config const cfg = {
		.session_quota      = { 256u << 20 },
		.arg_buffer_quota   = { 64u  << 10 },
		.trace_buffer_quota = { 92u  << 20 },
	};

	Tracer::init(genode_env(), cfg);

	struct Traced_thread
	{
		char const *name;
	} threads[] = {
		{ .name = "EMT" },
	};

	_max_tracer_id = 0;
	for (Traced_thread const &t : threads) {
		Tracer::Lookup_result const res = Tracer::lookup_subject("init -> vbox", t.name);
		if (!res.valid) {
			Genode::error("could not lookup ", t.name);
		} else {
			Genode::log("tracing ", t.name, " with id: ", res.id.value);
			Tracer::resume_tracing(res.id);
			_tracer_id[_max_tracer_id] = res.id;
			_max_tracer_id++;
		}
	}
#endif

	bool const use_timeout_fw = false;
	static Controller controller(*pThis->pci_ep, pci_device, timer_queue, use_timeout_fw);
	pThis->controller = &controller;

	/*
	 * Init instance data.
	 */
	pThis->pDevInsR3 = pDevIns;
	pThis->pDevInsR0 = PDMDEVINS_2_R0PTR(pDevIns);
	pThis->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);

	PCIDevSetVendorId      (&pThis->PciDev, 0xdead);
	PCIDevSetDeviceId      (&pThis->PciDev, 0xbeef);
	PCIDevSetClassProg     (&pThis->PciDev, 0x00);
	PCIDevSetClassSub      (&pThis->PciDev, 0x08);
	PCIDevSetClassBase     (&pThis->PciDev, 0x08);
	PCIDevSetInterruptPin  (&pThis->PciDev, 0x01);

	/*
	 * Register PCI device and I/O region.
	 */
	rc = PDMDevHlpPCIRegister(pDevIns, &pThis->PciDev);
	if (RT_FAILURE(rc))
		return rc;

	rc = PDMDevHlpPCIIORegionRegister(pDevIns, 0, pThis->controller->mmio_size(),
	                                  PCI_ADDRESS_SPACE_MEM, pcivboxR3Map);
	if (RT_FAILURE(rc))
		return rc;

	return VINF_SUCCESS;
}

const PDMDEVREG g_DevicePCIVBOX =
{
	/* u32version */
	PDM_DEVREG_VERSION,
	/* szName */
	"pci-vbox-ctl",
	/* szRCMod */
	"VBoxDDGC.gc",
	/* szR0Mod */
	"VBoxDDR0.r0",
	/* pszDescription */
	"PCI VBOX controller.\n",
	/* fFlags */
	PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RC,
	/* fClass */
	PDM_DEVREG_CLASS_BUS_USB,
	/* cMaxInstances */
	~0U,
	/* cbInstance */
	sizeof (PCIVBOX),
	/* pfnConstruct */
	pcivboxR3Construct,
	/* pfnDestruct */
	pcivboxDestruct,
	/* pfnRelocate */
	NULL,
	/* pfnMemSetup */
	NULL,
	/* pfnPowerOn */
	NULL,
	/* pfnReset */
	pcivboxReset,
	/* pfnSuspend */
	NULL,
	/* pfnResume */
	NULL,
	/* pfnAttach */
	NULL,
	/* pfnDetach */
	NULL, 
	/* pfnQueryInterface */
	NULL,
	/* pfnInitComplete */
	NULL,
	/* pfnPowerOff */
	NULL,
	/* pfnSoftReset */
	NULL,
	/* u32VersionEnd */
	PDM_DEVREG_VERSION
};


bool use_pci_controller()
{
	return true;
}
