/*
 * \brief  VM session interface test for aarch64
 * \author Alexander Boettcher
 * \date   2019-06-17
 *
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/signal.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>
#include <util/register.h>
#include <vm_session/connection.h>
#include <vm_session/vm_session.h>

#include <cpu/vm_state_virtualization.h>

struct Esr : Genode::Register<64>
{
	struct Ec : Bitfield<26, 6> { };
	struct Iss : Bitfield<0, 25> { };
};

class Vm;

class Vcpu {

	private:

		Vm                                 &_vm;
		Genode::Vm_connection              &_vm_con;
		Genode::Vm_handler<Vcpu>            _handler;
		Genode::Vm_session_client::Vcpu_id  _vcpu;
		Genode::Attached_dataspace          _state_ds;
		Genode::Vm_state                   &_state;

		/* test state start - some status information to steer the test */
		unsigned                            _exit_count     { 0 };
		unsigned                            _pause_count    { 0 };
		unsigned                            _timer_count    { 0 };
		unsigned                            _pause_at_timer { 0 };
		enum {
			INITIAL,
			HALTED,
			PAUSED,
			UNKNOWN,
			RUNNING
		}                                   _test_state { INITIAL };

		/* hold original state to compare it to state after exit */
		Genode::Vm_state                    _initial_state { };

		/* test state end */

		void _handle_vm_exception();
		bool _compare_vm_state(Genode::Vm_state &, Genode::Vm_state &);
		void _dump_vm_state(Genode::Vm_state &);

		void _cpu_init()
		{
			Genode::error("cpu init");
		}

	public:

		Vcpu(Genode::Entrypoint &ep, Genode::Vm_connection &vm_con,
		     Genode::Allocator &alloc, Genode::Env &env, Vm &vm)
		:
			_vm(vm), _vm_con(vm_con),
			_handler(ep, *this, &Vcpu::_handle_vm_exception),
			/* construct vcpu */
			_vcpu(_vm_con.with_upgrade([&]() {
				return _vm_con.create_vcpu(alloc, env, _handler); })),
			/* get state of vcpu */
			_state_ds(env.rm(), _vm_con.cpu_state(_vcpu)),
			_state(*_state_ds.local_addr<Genode::Vm_state>())
		{
			Genode::log("vcpu ", _vcpu.id, " : created");

			/* set initial vm state */
			int const reg = sizeof(_initial_state.r) / sizeof(_initial_state.r[0]);
			for (int i = 0; i < reg; i++)
				_initial_state.r[i]  = 0x100 + i;

			_initial_state.ip        = 0x1000;
			_initial_state.sp        = _initial_state.ip + 0x1000;

			_initial_state.far_el1   = ~0UL;
			_initial_state.esr_el1   = ~0U;
			_initial_state.cpacr_el1 = ~0U;
			_initial_state.mair_el1  = ~0UL;
			_initial_state.vbar_el1  = 0;
			_initial_state.par_el1   = ~0UL;
			_initial_state.tpidrro_el0 = ~0UL;
			_initial_state.tpidr_el0 = ~0UL;
			_initial_state.tpidr_el1 = ~0UL;
			_initial_state.amair_el1 = 0; /* hw implementation defined, see spec */
			_initial_state.actlr_el1 = 0; /* hw implementation defined, see spec */
			_initial_state.afsr0_el1 = 0; /* hw implementation defined, see spec */
			_initial_state.afsr1_el1 = 0; /* hw implementation defined, see spec */
			_initial_state.contextidr_el1 = 0;
			_initial_state.csselr_el1 = 0;

			_initial_state.sctlr_el1 = 0;
			_initial_state.sp_el0    = ~0UL;
			_initial_state.spsel     = 0x1;
			_initial_state.spsr_el1  = ~0U;
			_initial_state.daif      = 0x3c0;
			_initial_state.nzcv      = 0;
			_initial_state.fpcr      = 0;
			_initial_state.fpsr      = 0;

			int const qreg = sizeof(_initial_state.q) / sizeof(_initial_state.q[0]);
			for (int i = 0; i < qreg; i++)
				_initial_state.q[i]  = 0x100 + i;

			/* set first state */
			memcpy(&_state, &_initial_state, sizeof(_initial_state));
		}

		Genode::Vm_session_client::Vcpu_id id() const { return _vcpu; }

#if 0
		void skip_instruction(unsigned bytes)
		{
			_state = Genode::Vm_state {};
			_state.ip.value(_state.ip.value() + bytes);
		}
#endif

		/*
		 * state information and state requests to steer the test
		 */
		bool halted() const {
			 return _test_state == HALTED; }
		bool paused_1st() const {
			return _test_state == PAUSED && _pause_count == 1; }
		bool paused_2nd() const {
			return _test_state == PAUSED && _pause_count == 2; }
		bool paused_3rd() const {
			return _test_state == PAUSED && _pause_count == 3; }
		bool paused_4th() const {
			return _test_state == PAUSED && _pause_count == 4; }

		void break_endless_loop()
		{
			_pause_at_timer = _timer_count + 3;
		}
		bool pause_endless_loop()
		{
			if (_pause_at_timer == 0)
				return false;

			if (_timer_count < _pause_at_timer)
				return false;

			_pause_at_timer = 0;
			return true;
		}

		void claim_state_unknown() { _test_state = UNKNOWN; }

		void timer_triggered() { _timer_count ++; }
};

class Vm {

	private:

		enum { STACK_SIZE = 2*1024*sizeof(long) };

		Genode::Heap                 _heap;
		Genode::Vm_connection        _vm_con;
		Genode::Entrypoint          &_ep_first;  /* running on first CPU */
//		Genode::Entrypoint           _ep_second; /* running on second CPU */
		Vcpu                         _vcpu0;
//		Vcpu                         _vcpu1;
//		Vcpu                         _vcpu2;
//		Vcpu                         _vcpu3;
		Genode::Dataspace_capability _memory; /* guest memory */

		/* just to trigger some events after some time */
		Timer::Connection            _timer;
		Genode::Signal_handler<Vm>   _timer_handler;

		/* trigger destruction of _vm session to test this case also */
		Genode::Signal_context_capability _signal_destruction;

		void _handle_timer();

	public:

		Vm(Genode::Env &env, Genode::Signal_context_capability destruct_cap)
		:
			_heap(env.ram(), env.rm()),
			_vm_con(env),
			_ep_first(env.ep()),
//			_ep_second(env, STACK_SIZE, "second ep",
//			           env.cpu().affinity_space().location_of_index(1)),
			_vcpu0(_ep_first, _vm_con, _heap, env, *this),
//			_vcpu1(_ep_first, _vm_con, _heap, env, *this),
//			_vcpu2(_ep_second, _vm_con, _heap, env, *this),
//			_vcpu3(_ep_second, _vm_con, _heap, env, *this),
			_memory(env.ram().alloc(4096)),
			_timer(env),
			_timer_handler(_ep_first, *this, &Vm::_handle_timer),
			_signal_destruction(destruct_cap)
		{
			/* prepare guest memory with some instructions for testing */
			Genode::uint32_t * guest = env.rm().attach(_memory);
			*(guest + 0x0) = 0xd503201f; /* nop 0xd503201f */
			*(guest + 0x1) = 0xd503201f; /* nop 0xd503201f */
			*(guest + 0x2) = 0xd503201f; /* nop 0xd503201f */
			*(guest + 0x3) = 0xd503201f; /* nop 0xd503201f */
			*(guest + 0x4) = 0xd503201f; /* nop 0xd503201f */

			unsigned instr = 0;
#if 0
			*(guest + 0x1) = 0xd2800000; /* mov x0, #0x0 */
			*(guest + 0x1) = 0xd2800020; /* mov x0, #0x1 */
			*(guest + 0x2) = 0xd5181000; /* msr sctlr_el1, x0 */
#endif
#if 0
			*(guest + 0x1) = 0x9100001f; /* mov sp, x0 */
#endif

			/* GPR registers test */
			for (unsigned i = 0; i < 31; i++) {
				/* add xI, xI, #0x1 - I -> 0 ... 30 */
				*(guest + instr++) = 0x91000400UL + i * 0x20 + i;
			}

			/* change sp just for fun */
			*(guest + instr++) = 0x910013ffUL; /* add sp, sp, #0x4 */

			/* FPU registers test */
			for (unsigned i = 0; i < 32; i++) {
				/* add dI, dI, d31 - I -> 0 ... 31 */
				*(guest + instr++) = 0x5eff8400UL + i * 0x21;
			}

			Genode::error("instruction hvc 1");
			*(guest + instr++) = 0xd4000002; /* hvc instruction */

			Genode::error("instruction hvc 2");
			*(guest + instr++) = 0xd4000002; /* hvc instruction */

			Genode::error("instruction loop");
			*(guest + instr++) = 0x14000000; /* loop 1: b 1b 0x14000000 */

			env.rm().detach(guest);

			Genode::log("instruction generated ", Genode::Hex(instr),
			            " -> ip + ", Genode::Hex(instr * 4));

			/* XXX */
			_vm_con.attach(Vm::handle_guest_memory_exit(), 0x1000);
			/* XXX */

			Genode::log ("let vCPUs run - first EP");
			_vm_con.run(_vcpu0.id());
//			_vm_con.run(_vcpu1.id());

//			Genode::log ("let vCPUs run - second EP");
//			_vm_con.run(_vcpu2.id());
//			_vm_con.run(_vcpu3.id());

			_timer.sigh(_timer_handler);
			_timer.trigger_periodic(1000 * 1000 /* in us */);
		}

		Genode::Dataspace_capability handle_guest_memory_exit()
		{
			/*
			 * A real VMM would now have to lookup the right dataspace for
			 * the given guest physical region. This simple test has just one
			 * supported region ...
			 */
			return _memory;
		}
};


/**
 * Handle timer events - used to trigger some pause/resume on vCPU for testing
 */
void Vm::_handle_timer()
{
	_vcpu0.timer_triggered();
//	_vcpu1.timer_triggered();
//	_vcpu2.timer_triggered();
//	_vcpu3.timer_triggered();
	Genode::warning("timer triggered");
#if 0
	/*
	 * We're running in context of _ep_first. Try to trigger remotely
	 * for vCPU2 (handled by _ep_second actually) pause/run. Remotely means
	 * that vCPU2 is not on the same physical CPU as _ep_first.
	 */
	if (_vcpu2.halted()) {
		/* test to trigger a Genode signal even if we're already blocked */
		_vm_con.pause(_vcpu2.id());
	}

	if (_vcpu2.paused_1st()) {
		Genode::log(Genode::Thread::myself()->name(), "     : request resume of vcpu ", _vcpu2.id().id);

		/* continue after first paused state */
		_vm_con.run(_vcpu2.id());
	} else if (_vcpu2.paused_2nd()) {
		Genode::log(Genode::Thread::myself()->name(), "     : request resume of vcpu ", _vcpu2.id().id);

		/* skip over next hlt instructions after second paused state */
		_vcpu2.skip_instruction(1*1 /* 1x hlt instruction size */);

		/* reset state to unknown, otherwise we may enter this a second time */
		_vcpu2.claim_state_unknown();

		/* the next instruction is again a hlt */
		_vm_con.run(_vcpu2.id());
	}

	/*
	 * pause/run for vCPU0 in context of right _ep_first - meaning both
	 * are on same physical CPU.
	 */
	if (_vcpu1.pause_endless_loop()) {
		Genode::log("pause endless loop");
		/* guest in endless jmp loop - request to stop it asap */
		_vm_con.pause(_vcpu1.id());
		return;
	}

	if (_vcpu1.halted()) {
		Genode::log(Genode::Thread::myself()->name(), "     : request pause of vcpu ", _vcpu1.id().id);
		/* test to trigger a Genode signal even if we're already blocked */
		_vm_con.pause(_vcpu1.id());
	}

	if (_vcpu1.paused_1st()) {
		Genode::log(Genode::Thread::myself()->name(), "     : request resume (A) of vcpu ", _vcpu1.id().id);

		/* continue after first paused state */
		_vm_con.run(_vcpu1.id());
	} else if (_vcpu1.paused_2nd()) {
		Genode::log(Genode::Thread::myself()->name(), "     : request resume (B) of vcpu ", _vcpu1.id().id);

		/* skip over next 2 hlt instructions after second paused state */
		_vcpu1.skip_instruction(2*1 /* 2x hlt instruction size */);

		/* reset state to unknown, otherwise we may enter this a second time */
		_vcpu1.claim_state_unknown();

		/* the next instruction is actually a jmp endless loop */
		_vm_con.run(_vcpu1.id());

		/* request on the next timeout to stop the jmp endless loop */
		_vcpu1.break_endless_loop();
	} else if (_vcpu1.paused_3rd()) {
		Genode::log(Genode::Thread::myself()->name(), "     : request resume (C) of vcpu ", _vcpu1.id().id);

		_vcpu1.skip_instruction(1*2 /* 1x jmp endless loop size */);
		_vm_con.run(_vcpu1.id());
	} else if (_vcpu1.paused_4th()) {
		Genode::log("vcpu test finished - de-arm timer");
		_timer.trigger_periodic(0);

		/* trigger destruction of VM session */
		Genode::Signal_transmitter(_signal_destruction).submit();
	}
#endif
}

bool Vcpu::_compare_vm_state(Genode::Vm_state &state, Genode::Vm_state &expected)
{
	bool result = true;

	using namespace Genode;

	unsigned const compare = sizeof(state) / sizeof(addr_t);
	if (compare * sizeof(addr_t) != sizeof(state)) {
		error("vm_state size is unexpected");
		return false;
	}

	for (unsigned i = 0; i < compare; i++)
	{
		addr_t const s0 = *(reinterpret_cast<addr_t *>(&state) + i);
		addr_t const e0 = *(reinterpret_cast<addr_t *>(&expected) + i);

		if (s0 != e0) {
			error("vm state at ", i * sizeof(addr_t), " differs ",
			       Hex(s0), "!=", Hex(e0));
			result = false;
		}
	}

	return result;
}

void Vcpu::_dump_vm_state(Genode::Vm_state &state)
{
	unsigned size_dump = 0;

	for (unsigned i = 0; i < sizeof(state.r) / sizeof(state.r[0]); i++) {
		Genode::log(size_dump, ": x", i, "=", Genode::Hex(state.r[i]));
		size_dump += sizeof(state.r[i]);
	}

	Genode::log(size_dump, ": sp=", Genode::Hex(state.sp));
	size_dump += sizeof(state.sp);
	Genode::log(size_dump, ": ip=", Genode::Hex(state.ip));
	size_dump += sizeof(state.ip);

	Genode::log(size_dump, ": sctlr_el1=", Genode::Hex(_state.sctlr_el1));
	size_dump += sizeof(state.sctlr_el1);
	Genode::log(size_dump, ": ttbr0_el1=", Genode::Hex(_state.ttbr0_el1));
	size_dump += sizeof(state.ttbr0_el1);
	Genode::log(size_dump, ": ttbr1_el1=", Genode::Hex(_state.ttbr1_el1));
	size_dump += sizeof(state.ttbr1_el1);
	Genode::log(size_dump, ": tcr_el1=", Genode::Hex(_state.tcr_el1));
	size_dump += sizeof(state.tcr_el1);

	Genode::log(size_dump, ": elr_el1=", Genode::Hex(_state.elr_el1));
	size_dump += sizeof(state.elr_el1);
	Genode::log(size_dump, ": sp_el0=", Genode::Hex(_state.sp_el0));
	size_dump += sizeof(state.sp_el0);

	Genode::log(size_dump, ": far_el1=", Genode::Hex(_state.far_el1));
	size_dump += sizeof(state.far_el1);

	Genode::log(size_dump, ": mair_el1=", Genode::Hex(_state.mair_el1));
	size_dump += sizeof(state.mair_el1);

	Genode::log(size_dump, ": vbar_el1=", Genode::Hex(_state.vbar_el1));
	size_dump += sizeof(state.vbar_el1);

	Genode::log(size_dump, ": actlr_el1=", Genode::Hex(_state.actlr_el1));
	size_dump += sizeof(state.actlr_el1);

	Genode::log(size_dump, ": amair_el1=", Genode::Hex(_state.amair_el1));
	size_dump += sizeof(state.amair_el1);

	Genode::log(size_dump, ": par_el1=", Genode::Hex(_state.par_el1));
	size_dump += sizeof(state.par_el1);

	Genode::log(size_dump, ": tpidrro_el0=", Genode::Hex(_state.tpidrro_el0));
	size_dump += sizeof(state.tpidrro_el0);

	Genode::log(size_dump, ": tpidr_el0=", Genode::Hex(_state.tpidr_el0));
	size_dump += sizeof(state.tpidr_el0);

	Genode::log(size_dump, ": tpidr_el1=", Genode::Hex(_state.tpidr_el1));
	size_dump += sizeof(state.tpidr_el1);

	Genode::log(size_dump, ": spsel=", Genode::Hex(_state.spsel));
	size_dump += sizeof(state.spsel);
	Genode::log(size_dump, ": spsr_el1=", Genode::Hex(_state.spsr_el1));
	size_dump += sizeof(state.spsr_el1);

	Genode::log(size_dump, ": daif=", Genode::Hex(_state.daif));
	size_dump += sizeof(state.daif);
	Genode::log(size_dump, ": nzcv=", Genode::Hex(_state.nzcv));
	size_dump += sizeof(state.nzcv);
	Genode::log(size_dump, ": fpcr=", Genode::Hex(_state.fpcr));
	size_dump += sizeof(state.fpcr);
	Genode::log(size_dump, ": fpsr=", Genode::Hex(_state.fpsr));
	size_dump += sizeof(state.fpsr);

	Genode::log(size_dump, ": esr_el1=", Genode::Hex(_state.esr_el1));
	size_dump += sizeof(state.esr_el1);

	Genode::log(size_dump, ": cpacr_el1=", Genode::Hex(_state.cpacr_el1));
	size_dump += sizeof(state.cpacr_el1);

	Genode::log(size_dump, ": afsr0_el1=", Genode::Hex(_state.afsr0_el1));
	size_dump += sizeof(state.afsr0_el1);

	Genode::log(size_dump, ": afsr1_el1=", Genode::Hex(_state.afsr1_el1));
	size_dump += sizeof(state.afsr1_el1);

	Genode::log(size_dump, ": contextidr_el1=", Genode::Hex(_state.contextidr_el1));
	size_dump += sizeof(state.contextidr_el1);

	Genode::log(size_dump, ": csselr_el1=", Genode::Hex(_state.csselr_el1));
	size_dump += sizeof(state.csselr_el1);

	Genode::log(size_dump, ": esr_el2=", Genode::Hex(_state.esr_el2));
	size_dump += sizeof(state.esr_el2);

	size_dump += sizeof(state.unused);

	for (unsigned i = 0; i < sizeof(state.q) / sizeof(state.q[0]); i++) {
		Genode::log(size_dump, ": q", i, "=", Genode::Hex(state.q[i]));
		size_dump += sizeof(state.q[i]);
	}

	if (size_dump != sizeof(state)) {
		Genode::error("dump incomplete, missing fields ",
		              size_dump, "!=", sizeof(state));
	}
}

/**
 * Handle VM exits ...
 */
void Vcpu::_handle_vm_exception()
{
	_exit_count++;

	using namespace Genode;

	Esr::access_t esr = _state.esr_el2;

	warning(_exit_count, ": vm exit - ip=", Genode::Hex(_state.ip),
	        " ", Genode::Hex(Esr::Ec::get(esr)), " ",
	        " ", Genode::Hex(Esr::Iss::get(esr)));

	if (_exit_count == 1) {
		log ("got first hvc exit -> run until next hvc");

		Vm_state expected_state = _initial_state;
		int const reg = sizeof(_initial_state.r) / sizeof(_initial_state.r[0]);
		for (int i = 0; i < reg; i++)
			expected_state.r[i] += 1;

		expected_state.esr_el2 = 0x5a000000;
		expected_state.sp += 0x4;
		expected_state.ip += 0x104;

		int const qreg = sizeof(_initial_state.q) / sizeof(_initial_state.q[0]);
		for (int i = 0; i < qreg; i++)
			expected_state.q[i] += _initial_state.q[31];

		if (!_compare_vm_state(expected_state, _state)) {
			_dump_vm_state(_state);
			_test_state = UNKNOWN;
			return;
		}
	} else
	if (_exit_count == 2) {
		log ("got second hvc exit -> go in while loop");
		// just continue */
	} else {
		Genode::error("unknown exit ", _exit_count);
		_test_state = UNKNOWN;
		return;
	}

	log("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - resume vcpu");

	_test_state = RUNNING;
	_vm_con.run(_vcpu);

#if 0
	enum { VMEXIT_STARTUP = 0xfe, VMEXIT_PAUSED = 0xff };

	unsigned const exit = _state.exit_reason;

	_state = Vm_state {};

	log("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - ",
	    "reason ", Hex(exit), " handled by '",
	    Genode::Thread::myself()->name(), "'");

	if (exit == VMEXIT_STARTUP) {
		_cpu_init();
	}

	if (exit == Intel_exit::INVALID_STATE) {
		error("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - "
		      " halting vCPU - invalid guest state");
		_test_state = UNKNOWN;
		return;
	}

	if (exit == Amd_exit::TRIPLE_FAULT) {
		error("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - "
		      " halting vCPU - triple fault");
		_test_state = UNKNOWN;
		return;
	}

	if (exit == VMEXIT_PAUSED) {
		log("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - "
		    " due to pause() request - ip=", Hex(_state.ip.value()));
		_pause_count ++;
		_test_state = PAUSED;
		return;
	}

	if (exit == Intel_exit::HLT || exit == Amd_exit::HLT) {
		log("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - "
		    " halting vCPU - guest called HLT - ip=", Hex(_state.ip.value()));
		_test_state = HALTED;
		return;
	}

	if (exit == Intel_exit::EPT ||
	    exit == Amd_exit::NPT || exit == Amd_exit::PF)
	{
		addr_t const guest_fault_addr = _state.qual_secondary.value();
		addr_t const guest_map_addr   = _state.qual_secondary.value() & ~0xFFFUL;

		log("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - "
		    " guest fault address: ", Hex(guest_fault_addr));

		Dataspace_capability cap = _vm.handle_guest_memory_exit();
		if (!cap.valid()) {
			error("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - "
			      " halting vCPU - guest memory lookup failed");
			_test_state = UNKNOWN;
			/* no memory - we halt the vcpu */
			return;
		}
		if (guest_fault_addr != 0xfffffff0UL) {
			error("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - "
			      " unknown guest fault address");
			return;
		}

		_vm_con.attach(cap, guest_map_addr);
	}

	if (_exit_count >= 5) {
		error("vcpu ", _vcpu.id, " : ", _exit_count, ". vm exit - "
		      " halting vCPU - unknown state");
		_test_state = UNKNOWN;
		return;
	}
#endif
}

class Vmm {

	private:

		Genode::Signal_handler<Vmm> _destruct_handler;
		Genode::Reconstructible<Vm> _vm;

		void _destruct()
		{
			Genode::log("destruct vm session");

			_vm.destruct();

			Genode::log("vmm test finished");
		}

	public:

		Vmm(Genode::Env &env)
		:
			_destruct_handler(env.ep(), *this, &Vmm::_destruct),
			_vm(env, _destruct_handler)
		{
		}
};

void Component::construct(Genode::Env & env) { static Vmm vmm(env); }
