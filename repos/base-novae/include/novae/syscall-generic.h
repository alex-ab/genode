/*
 * \brief  Syscall bindings for the NOVA microhypervisor
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \author Alexander Boettcher
 * \date   2009-12-27
 */

/*
 * Copyright (c) 2009-2024 Genode Labs
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <novae/stdint.h>

namespace Novae {

	enum {
		PAGE_SIZE_LOG2 = 12,
		PAGE_SIZE_BYTE = 1 << PAGE_SIZE_LOG2,
		PAGE_MASK_ = ~(PAGE_SIZE_BYTE - 1)
	};

	/**
	 * NOVA system-call IDs
	 */
	enum Syscall {
		NOVA_CALL       = 0x0,
		NOVA_REPLY      = 0x1,
		NOVA_CREATE_PD  = 0x2,
		NOVA_CREATE_EC  = 0x3,
		NOVA_CREATE_SC  = 0x4,
		NOVA_CREATE_PT  = 0x5,
		NOVA_CREATE_SM  = 0x6,
		NOVA_PD_CTRL    = 0x7,
		NOVA_EC_CTRL    = 0x8,
		NOVA_SC_CTRL    = 0x9,
		NOVA_PT_CTRL    = 0xa,
		NOVA_SM_CTRL    = 0xb,
		NOVA_HW_CTRL    = 0xc,
		NOVA_ASSIGN_INT = 0xd,
		NOVA_ASSIGN_DEV = 0xe,
	};

	/**
	 * NOVA status codes returned by system-calls
	 */
	enum Status
	{
		NOVA_OK             = 0,
		NOVA_TIMEOUT        = 1,
		NOVA_ABORTED        = 2,
		NOVA_OVERFLOW       = 3,
		NOVA_INV_HYPERCALL  = 4,
		NOVA_INV_SELECTOR   = 5,
		NOVA_INV_PARAMETER  = 6,
		NOVA_INV_FEATURE    = 7,
		NOVA_INV_CPU        = 8,
		NOVA_INVD_DEVICE_ID = 9,
		NOVA_MEM_OBJ        = 10,
		NOVA_MEM_CAP        = 11,
	};

	/**
	 * Hypervisor information page
	 */
	struct Hip
	{
		mword_t raw[512];

		auto signature()       const { return uint32_t(raw[0x00 / 8]); }
		auto nova_addr_start() const { return         (raw[0x08 / 8]); }
		auto nova_addr_end()   const { return         (raw[0x10 / 8]); }
		auto mbuf_addr_start() const { return         (raw[0x18 / 8]); }
		auto mbuf_addr_end()   const { return         (raw[0x20 / 8]); }
		auto root_addr_start() const { return         (raw[0x28 / 8]); }
		auto root_addr_end()   const { return         (raw[0x30 / 8]); }

		auto timer_freq()      const { return         (raw[0x50 / 8]); }

		auto sel_num () const { return 1u << uint8_t (raw[0x58 / 8] >>  0); }
		auto cpu_bsp () const { return       uint16_t(raw[0x58 / 8] >> 48); }
		auto gsi_pin () const { return       uint16_t(raw[0x60 / 8] >> 48); }
		auto gsi_max () const { return          1u + ((raw[0x68 / 8] >> 16) & 0xfffful);  }
		auto cpu_max () const { return uint32_t(1u + ((raw[0x68 / 8] >> 32) & 0xfffful)); }
		auto vec_max () const { return uint16_t(      (raw[0x68 / 8] >> 48)); }

		auto sel_hst_arch() const { return uint16_t(raw[0x70 / 8] >>  0); }
		auto sel_hst_nova() const { return uint16_t(raw[0x70 / 8] >> 16); }

		auto features()     const { return uint64_t(raw[0x78 / 8]); }

		bool has_feature_iommu() const { return !!(features() & (1 << 0)); }
		bool has_feature_vmx()   const { return !!(features() & (1 << 1)); }
		bool has_feature_svm()   const { return !!(features() & (1 << 2)); }

	} __attribute__((packed));


	/**
	 * Semaphore operations
	 */
	enum Sem_op { SEMAPHORE_UP = 0U, SEMAPHORE_DOWN = 1U, SEMAPHORE_DOWNZERO = 0x3U };

	/**
	 * Ec operations
	 */
	enum Ec_op {
		EC_RECALL = 0U,
		EC_TIME = 5U,
		EC_GET_VCPU_STATE = 6U,
		EC_SET_VCPU_STATE = 7U,
	};

	class Gsi_flags
	{
		private:

			uint8_t _value { 0 };

		public:

			enum Mode { HIGH, LOW, EDGE };

			Gsi_flags() { }

			Gsi_flags(Mode m)
			{
				/* host owned (3. bit), masked (0. bit) */
				switch (m) {
				case HIGH: _value = 0b0011; break; /* level-high */
				case LOW:  _value = 0b0111; break; /* level-low */
				case EDGE: _value = 0b0001; break; /* edge-triggered */
				}
			}

			uint8_t value() const { return _value; }
	};


	class Descriptor
	{
		protected:

			mword_t _value { 0 };

			/**
			 * Assign bitfield to descriptor
			 */
			template<mword_t MASK, mword_t SHIFT>
			void _assign(mword_t new_bits)
			{
				_value &= ~(MASK << SHIFT);
				_value |= (new_bits & MASK) << SHIFT;
			}

			/**
			 * Query bitfield from descriptor
			 */
			template<mword_t MASK, mword_t SHIFT>
			mword_t _query() const { return (_value >> SHIFT) & MASK; }

		public:

			mword_t value() const { return _value; }

	} __attribute__((packed));


	/**
	 * Message-transfer descriptor
	 */
	class Mtd
	{
		private:

			mword_t const _value;

		public:

			enum {
				GPR_0_7        = 1U << 1,
				GPR_8_15       = 1U << 2,
				EFL            = 1U << 3,
				EIP            = 1U << 4,
				QUAL           = 1U << 6,  /* exit qualification */

				ESDS           = 1U << 5,
				FSGS           = 1U << 6,
				CSSS           = 1U << 7,
				TR             = 1U << 8,
				LDTR           = 1U << 9,
				GDTR           = 1U << 10,
				IDTR           = 1U << 11,
				CR             = 1U << 12,
				DR             = 1U << 13,  /* DR7 */
				SYS            = 1U << 14,  /* Sysenter MSRs CS, ESP, EIP */
				CTRL           = 1U << 16,  /* execution controls */
				INJ            = 1U << 17,  /* injection info */
				STA            = 1U << 18,  /* interruptibility state */
				TSC            = 1U << 19,  /* time-stamp counter */
				EFER           = 1U << 20,  /* EFER MSR */
				PDPTE          = 1U << 21,  /* PDPTE0 .. PDPTE3 */
				SYSCALL_SWAPGS = 1U << 23,  /* SYSCALL and SWAPGS MSRs */
				TPR            = 1U << 24,  /* TPR and TPR threshold */
				TSC_AUX        = 1U << 25,  /* IA32_TSC_AUX used by rdtscp */
				XSAVE          = 1U << 26,  /* XCR and XSS used with XSAVE */
				FPU            = 1U << 31,  /* FPU state */

				IRQ   = EFL | STA | INJ | TSC,
				ALL   = (0x000fffff & ~CTRL) | EFER | GPR_0_7 | GPR_8_15 | SYSCALL_SWAPGS | TPR,
			};

			Mtd(mword_t value) : _value(value) { }

			mword_t value() const { return _value; }
	};


	class Crd : public Descriptor
	{
		protected:

			/**
			 * Bitfield holding the descriptor type
			 */
			enum {
				TYPE_MASK   = 0x3,  TYPE_SHIFT  =  0,
				BASE_SHIFT  = 12,   RIGHTS_MASK = 0x1f,
				ORDER_MASK  = 0x1f, ORDER_SHIFT =  7,
				BASE_MASK   = (~0UL) >> BASE_SHIFT,
				RIGHTS_SHIFT= 2
			};

			/**
			 * Capability-range-descriptor types
			 */
			enum {
				NULL_CRD_TYPE   = 0,
				MEM_CRD_TYPE    = 1,
				IO_CRD_TYPE     = 2,
				OBJ_CRD_TYPE    = 3,
				RIGHTS_ALL      = 0x1f,
			};

			void _base(mword_t base)
			{ _assign<BASE_MASK, BASE_SHIFT>(base); }

			void _order(mword_t order)
			{ _assign<ORDER_MASK, ORDER_SHIFT>(order); }

		public:

			Crd(mword_t base, mword_t order) {
				_value = 0; _base(base), _order(order); }

			Crd(mword_t value) { _value = value; }

			mword_t hotspot(mword_t sel_hotspot) const
			{
				if ((value() & TYPE_MASK) == MEM_CRD_TYPE)
					return sel_hotspot & PAGE_MASK_;

				return sel_hotspot << 12;
			}

			mword_t addr()   const { return base() << BASE_SHIFT; }
			mword_t base()   const { return _query<BASE_MASK, BASE_SHIFT>(); }
			mword_t order()  const { return _query<ORDER_MASK, ORDER_SHIFT>(); }
			bool is_null()   const { return (_value & TYPE_MASK) == NULL_CRD_TYPE; }
			uint8_t type()   const { return (uint8_t)_query<TYPE_MASK, TYPE_SHIFT>(); }
			uint8_t rights() const { return (uint8_t)_query<RIGHTS_MASK, RIGHTS_SHIFT>(); }
	} __attribute__((packed));


	class Rights
	{
		private:

			bool const _readable, _writeable, _executable;

		public:

			Rights(bool readable, bool writeable, bool executable)
			: _readable(readable), _writeable(writeable),
			  _executable(executable) { }

			Rights() : _readable(false), _writeable(false), _executable(false) {}

			bool readable()   const { return _readable; }
			bool writeable()  const { return _writeable; }
			bool executable() const { return _executable; }

			static auto read_only() { return Rights( true, false, false); }
			static auto rw()        { return Rights( true,  true, false); }
			static auto none()      { return Rights(false, false, false); }
	};


	struct Cacheability {
		static unsigned write_back()      { return 0; }
		static unsigned write_through()   { return 1; }
		static unsigned write_combining() { return 2; }
		static unsigned uncacheable()     { return 3; }
		static unsigned write_protected() { return 4; }
	};


	/**
	 * Memory-capability-range descriptor
	 */
	class Mem_crd : public Crd
	{
		private:

			enum {
				EXEC_MASK  = 0x1, EXEC_SHIFT  =  4,
				WRITE_MASK = 0x1, WRITE_SHIFT =  3,
				READ_MASK  = 0x1, READ_SHIFT  =  2
			};

			void _rights(Rights r)
			{
				_assign<EXEC_MASK,  EXEC_SHIFT>(r.executable());
				_assign<WRITE_MASK, WRITE_SHIFT>(r.writeable());
				_assign<READ_MASK,  READ_SHIFT>(r.readable());
			}

		public:

			Mem_crd(mword_t base, mword_t order, Rights rights = Rights())
			: Crd(base, order)
			{
				_rights(rights);
				_assign<TYPE_MASK, TYPE_SHIFT>(MEM_CRD_TYPE);
			}

			Rights rights() const
			{
				return Rights(_query<READ_MASK,  READ_SHIFT>(),
				              _query<WRITE_MASK, WRITE_SHIFT>(),
				              _query<EXEC_MASK,  EXEC_SHIFT>());
			}
	};


	/**
	 * I/O-capability-range descriptor
	 */
	class Io_crd : public Crd
	{
		public:

			Io_crd(mword_t base, mword_t order)
			: Crd(base, order)
			{
				_assign<TYPE_MASK, TYPE_SHIFT>(IO_CRD_TYPE);
				_assign<RIGHTS_MASK, RIGHTS_SHIFT>(RIGHTS_ALL);
			}
	};


	class Obj_crd : public Crd
	{
		public:

			enum {
				RIGHT_EC_RECALL = 0x1U,
				RIGHT_PT_CTRL   = 0x1U,
				RIGHT_PT_CALL   = 0x2U,
				RIGHT_PT_EVENT  = 0x4U,
				RIGHT_SM_UP     = 0x1U,
				RIGHT_SM_DOWN   = 0x2U
			};

			Obj_crd() : Crd(0, 0)
			{
				_assign<TYPE_MASK, TYPE_SHIFT>(NULL_CRD_TYPE);
			}
	
			Obj_crd(mword_t base, mword_t order,
			        mword_t rights = RIGHTS_ALL)
			: Crd(base, order)
			{
				_assign<TYPE_MASK, TYPE_SHIFT>(OBJ_CRD_TYPE);
				_assign<RIGHTS_MASK, RIGHTS_SHIFT>(rights);
			}
	};


	/**
	 * Quantum-priority descriptor
	 */
	class Qpd : public Descriptor
	{
		private:

			enum {
				PRIORITY_MASK = 0x7ful, PRIORITY_SHIFT = 16,
				QUANTUM_SHIFT = 0,
				QUANTUM_MASK  = (1ul << 16) - 1
			};

			void _quantum(mword_t quantum)
			{ _assign<QUANTUM_MASK, QUANTUM_SHIFT>(quantum); }

			void _priority(mword_t priority)
			{ _assign<PRIORITY_MASK, PRIORITY_SHIFT>(priority); }

		public:

			enum { DEFAULT_QUANTUM = 10000, DEFAULT_PRIORITY = 64 };

			Qpd(mword_t quantum  = DEFAULT_QUANTUM,
			    mword_t priority = DEFAULT_PRIORITY)
			{
				_value = 0;
				_quantum(quantum), _priority(priority);
			}

			mword_t quantum()  const { return _query<QUANTUM_MASK,  QUANTUM_SHIFT>(); }
			mword_t priority() const { return _query<PRIORITY_MASK, PRIORITY_SHIFT>(); }
	};


	/**
	 * User-level thread-control block
	 */
	struct Utcb
	{
		/**
		 * Return physical size of UTCB in bytes
		 */
		static constexpr mword_t size() { return 4096; }

		mword_t mr[512];

		mword_t * msg() { return mr; }

		auto ax (auto const value) { mr[0x00 / 8] = value; }
		auto cx (auto const value) { mr[0x08 / 8] = value; }
		auto dx (auto const value) { mr[0x10 / 8] = value; }
		auto bx (auto const value) { mr[0x18 / 8] = value; }
		auto sp (auto const value) { mr[0x20 / 8] = value; }
		auto bp (auto const value) { mr[0x28 / 8] = value; }
		auto si (auto const value) { mr[0x30 / 8] = value; }
		auto di (auto const value) { mr[0x38 / 8] = value; }
		auto r8 (auto const value) { mr[0x40 / 8] = value; }
		auto r9 (auto const value) { mr[0x48 / 8] = value; }
		auto r10(auto const value) { mr[0x50 / 8] = value; }
		auto r11(auto const value) { mr[0x58 / 8] = value; }
		auto r12(auto const value) { mr[0x60 / 8] = value; }
		auto r13(auto const value) { mr[0x68 / 8] = value; }
		auto r14(auto const value) { mr[0x70 / 8] = value; }
		auto r15(auto const value) { mr[0x78 / 8] = value; }
		auto fl (auto const value) { mr[0x80 / 8] = value; }
		auto ip (auto const value) { mr[0x88 / 8] = value; }

		auto ax () const { return mr[0x00 / 8]; }
		auto cx () const { return mr[0x08 / 8]; }
		auto dx () const { return mr[0x10 / 8]; }
		auto bx () const { return mr[0x18 / 8]; }
		auto sp () const { return mr[0x20 / 8]; }
		auto bp () const { return mr[0x28 / 8]; }
		auto si () const { return mr[0x30 / 8]; }
		auto di () const { return mr[0x38 / 8]; }
		auto r8 () const { return mr[0x40 / 8]; }
		auto r9 () const { return mr[0x48 / 8]; }
		auto r10() const { return mr[0x50 / 8]; }
		auto r11() const { return mr[0x58 / 8]; }
		auto r12() const { return mr[0x60 / 8]; }
		auto r13() const { return mr[0x68 / 8]; }
		auto r14() const { return mr[0x70 / 8]; }
		auto r15() const { return mr[0x78 / 8]; }
		auto fl () const { return mr[0x80 / 8]; }
		auto ip () const { return mr[0x88 / 8]; }

		auto    qual_1()  const { return         mr[0xa0 / 8]; }
		auto    pf_addr() const { return         mr[0xa8 / 8]; }
		uint8_t pf_type() const { return uint8_t(mr[0xa0 / 8]); }

	};

	static_assert(sizeof(Utcb) == 4096, "Unexpected size of UTCB");

	/**
	 * Size of event-specific portal window mapped at PD creation time
	 */
	enum {
		NUM_PT_ARCH_LOG2         = 5,
		NUM_INITIAL_PT_LOG2      = NUM_PT_ARCH_LOG2 + 1,
		NUM_INITIAL_PT           = 1UL << NUM_INITIAL_PT_LOG2,
		NUM_INITIAL_PT_RESERVED  = 2 * NUM_INITIAL_PT,
		NUM_INITIAL_VCPU_PT_LOG2 = 8,
		NUM_INITIAL_VCPU_PT      = 1UL << NUM_INITIAL_VCPU_PT_LOG2,
	};

	/**
	 * Event-specific capability selectors
	 */
	enum {
		PT_SEL_PAGE_FAULT = 0x0e,  /* x86 architectural */
		PT_SEL_STARTUP    = 0x20,  /* defined by NOVA spec */
		PT_SEL_RECALL     = 0x21,  /* defined by NOVA spec */
		PT_SEL_DELEGATE   = 0x22,  /* convention on Genode */
		SM_SEL_EC         = 0x23,  /* convention on Genode */
		PT_SEL_PARENT     = 0x24,  /* convention on Genode */
		EC_SEL_THREAD     = 0x25,  /* convention on Genode */
		SM_SEL_SIGNAL     = 0x26,  /* convention on Genode */
	};
}
