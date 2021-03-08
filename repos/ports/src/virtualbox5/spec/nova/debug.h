#ifndef _VIRTUALBOX__SPEC__NOVA__DEBUG_H_
#define _VIRTUALBOX__SPEC__NOVA__DEBUG_H_

#include <trace/timestamp.h>

struct Debug_monitor
{

	Tracer::Id               trace_id    { };
	Genode::Trace::Timestamp start_trace { };

	unsigned long vm_exit_count  = 0;
	unsigned long vm_exit_reason = 0;
	unsigned long exit_qualifier = 0;

	Genode::Trace::Timestamp hw_in  { };
	Genode::Trace::Timestamp hw_out { };

	unsigned long exit_lapic  = 0;
	unsigned long exit_ioapic = 0;

	struct {
		unsigned long count;
	} vm_exits [256] { };

	Genode::Trace::Timestamp tsc_vmm { };

	inline void start_tracing(char const *, char const *);
	inline void stop_tracing();

	inline void debug_enter_hw();
	inline void debug_leave_hw(unsigned long, unsigned long);
	inline void debug_dump();
	inline char const * const name_exit(unsigned) const;
};

void Debug_monitor::start_tracing(char const *label, char const *thread)
{
	/* XXX label */
	Tracer::Lookup_result const res = Tracer::lookup_subject(label, thread);
	if (res.valid) {
		trace_id = res.id;

		Tracer::resume_tracing(trace_id);

		Genode::log("tracing ", thread, " with id: ", trace_id.value);

		start_trace = Genode::Trace::timestamp();
	} else
		Genode::error("lookup of ", thread, " for tracing failed");
}

void Debug_monitor::stop_tracing()
{
	start_trace   = 0;
	vm_exit_count = 0;
	Tracer::pause_tracing(trace_id);

	for (unsigned i = 0; i < sizeof(vm_exits) / sizeof(vm_exits[0]); i++)
		vm_exits[i].count = 0;
}

void Debug_monitor::debug_enter_hw()
{
	hw_in   = Genode::Trace::timestamp();
	tsc_vmm = hw_in - hw_out;
}

char const * const Debug_monitor::name_exit(unsigned exit_reason) const
{
	switch (exit_reason) {
		case VMX_EXIT_EPT_VIOLATION : return "EPT";
		case VMX_EXIT_IO_INSTR : return "IO";
		case VMX_EXIT_CPUID : return "CPUID";
		case VMX_EXIT_HLT : return "HALT";
		case VMX_EXIT_RDMSR : return "RDMSR";
		case VMX_EXIT_WRMSR : return "WRMSR";
		case 255 : return "RECALL";
		default: return "?";
	}
}

void Debug_monitor::debug_leave_hw(unsigned long const exit,
                                   unsigned long const exit_qual_1)
{
	vm_exit_count ++;

	hw_out = Genode::Trace::timestamp();

	vm_exit_reason = exit;
	exit_qualifier = exit_qual_1;

	using Genode::Trace::Timestamp;

	if (!start_trace)
		return;

	if (vm_exit_reason >= sizeof(vm_exits) / sizeof(vm_exits[0])) {
		Genode::error("too large exit reason ", vm_exit_reason);
		return;
	}
	vm_exits[vm_exit_reason].count ++;

	bool create_trace = true;

	switch (vm_exit_reason) {
	case VMX_EXIT_EPT_VIOLATION:
		if (0xfee00000 <= exit_qualifier && exit_qualifier < 0xfee01000) {
			exit_lapic ++;
			create_trace = false;
		} else
		if (0xfec00000 <= exit_qualifier && exit_qualifier < 0xfec01000) {
			exit_ioapic ++;
			create_trace = false;
		}
		create_trace = false;
		break;
	case VMX_EXIT_RDMSR:
	case VMX_EXIT_WRMSR:
	case VMX_EXIT_IO_INSTR:
	case VMX_EXIT_CPUID:
		create_trace = false;
		break;
	default:
//		create_trace = false;
		break;
	}

	if (create_trace) {
		Timestamp const tsc_vm  = hw_out - hw_in;
		uint64_t  const diff_start_us = (hw_out - start_trace) / (genode_cpu_hz() / 1000 / 1000);
		uint64_t  const diff_vm_us  = (tsc_vm)  / (genode_cpu_hz() / 1000 / 1000);
		uint64_t  const diff_vmm_us = (tsc_vmm) / (genode_cpu_hz() / 1000 / 1000);

		Genode::trace(Genode::Thread::myself()->name(),
		              ": ",
		              vm_exit_count, ". - ",
		              diff_start_us < 100 ? " " : "",
		              diff_start_us < 10  ? " " : "",
		              diff_start_us, " us, vmm=",
		              diff_vmm_us < 100 ? " " : "",
		              diff_vmm_us < 10  ? " " : "",
		              diff_vmm_us, " us, vm=",
		              diff_vm_us < 100 ? " " : "",
		              diff_vm_us < 10  ? " " : "",
		              diff_vm_us, " us, ",
		              "exit=", vm_exit_reason, " ",
		              name_exit(vm_exit_reason),
		              (vm_exit_reason == VMX_EXIT_EPT_VIOLATION) ?
		              Genode::String<32>(" ept_qual[1]=", Genode::Hex(exit_qualifier)) : Genode::String<32>(""));
	}
}

void Debug_monitor::debug_dump()
{
	if (vm_exit_reason >= sizeof(vm_exits) / sizeof(vm_exits[0])) {
		Genode::error("too large exit reason ", vm_exit_reason);
		return;
	}

	Tracer::dump_trace_buffer(trace_id);

	Genode::String<1024> output { vm_exit_count, "." };
	for (unsigned i = 0; i < sizeof(vm_exits) / sizeof(vm_exits[0]); i++) {
		if (vm_exits[i].count > 0)
			output = Genode::String<1024>(output, " ", name_exit(i), "(", i,
			                              ")=", vm_exits[i].count);
	}

	Genode::log(output);
	Genode::log("  ioapic=", exit_ioapic, " lapic=", exit_lapic);
}

#endif /* _VIRTUALBOX__SPEC__NOVA__DEBUG_H_ */
