#include "sup.h"

static volatile guest_tsc _tsc_cpus[32];

guest_tsc genode_vm_enter_tsc(guest_tsc const tsc_on_exit)
{
	guest_tsc ret { 0, 0 };

	if (!tsc_on_exit.tsc)
		return ret;

#if 0
	bool tsc_offset = TMCpuTickCanUseRealTSC(pVM, pVCpu,
	                                         &pVCpu->hm.s.vmx.u64TSCOffset,
	                                         &fParavirtTsc);

	TMNotifyStartOfExecution(pVCpu);
#endif

	uint64_t const guest_past_tsc = tsc_on_exit.tsc + tsc_on_exit.offset;
	uint64_t       guest_max_tsc  = 0;

/* XXX synchronize reading here with writing on end of function */
	/* lookup max seen guest tsc value */
	for (unsigned i = 0; i < sizeof(_tsc_cpus) / sizeof(_tsc_cpus[0]); i++) {
		guest_tsc volatile &cpu = _tsc_cpus[i];

		if (!cpu.tsc) continue;

		if (cpu.tsc + cpu.offset > guest_past_tsc)
			guest_max_tsc = cpu.tsc + cpu.offset;
	}

	/* max diff threshold between CPUs: 10us */
	uint64_t const threshold_tsc = genode_cpu_hz() / 1000 / 100;
	if (guest_max_tsc > guest_past_tsc + threshold_tsc) {
		ret.offset = (guest_max_tsc - guest_past_tsc) / 3; /* dynamic factor calculation XXX */
	}

	/*
	 * To keep the same virtual tsc for the guest, we have to
	 * recalculate the tsc_offset value. The tsc_on_exit.tsc changes
	 * automatically as soon as we resume, since it is the hw tsc.
	 * Accordingly, the virtual tsc would jump if we do not adjust
	 * tsc_offset. Additionally, we have to incorporate the current
	 * tsc, since we theoretically can spend endless time within
	 * the VMM.
	 *
	 * The kernel supports not setting the new offset value,
	 * instead it just adds an offset. So, in order to "set" the
	 * new offset, we have to subtract first the previous
	 * tsc_on_exit.offset value and then add our calculated new offset.
	 */

/* XXX - only works with kernel patch, e.g. not adding tsc offset when vCPU caused an exit (has partner)
@@ -110,7 +118,9 @@ void Sc::ready_dequeue (uint64 t)
 
     trace (TRACE_SCHEDULE, "DEQ:%p (%llu) PRIO:%#x TOP:%#x", this, left, prio, prio_top);
 
-    ec->add_tsc_offset (tsc - t);
+    bool const dont_add = !ec->utcb && ec->partner;
+    if (!dont_add)
+        ec->add_tsc_offset (tsc - t);
 
     tsc = t;
 }
*/

	uint64_t const host_current_tsc = ASMReadTSC();
	ret.offset += - tsc_on_exit.offset
	            + (guest_past_tsc - host_current_tsc);

	ret.tsc = 1;

	return ret;
}

void genode_vm_exit_tsc(unsigned const cpu_id, guest_tsc const tsc_on_exit)
{
	using namespace Genode;

	if (cpu_id >= sizeof(_tsc_cpus) / sizeof(_tsc_cpus[0])) {
		error("unsupported CPU ", cpu_id);
		return;
	}

/* XXX synchronize writing here with reading on top of function */
	guest_tsc const last_exit = { .tsc = _tsc_cpus[cpu_id].tsc,
	                              .offset = _tsc_cpus[cpu_id].offset };

	_tsc_cpus[cpu_id].tsc    = tsc_on_exit.tsc;
	_tsc_cpus[cpu_id].offset = tsc_on_exit.offset;

	bool show_debug_message = !tsc_on_exit.tsc;
	if (tsc_on_exit.tsc) {
		if (last_exit.tsc + last_exit.offset > tsc_on_exit.tsc + tsc_on_exit.offset) {
			error("tsc running backwards A");
			show_debug_message = true;
		}
		if (tsc_on_exit.tsc < last_exit.tsc) {
			show_debug_message = true;
			error("tsc running backwards B");
		}
//		TMCpuTickSetLastSeen(pVCpu, tsc_on_exit.tsc + tsc_on_exit.offset);
	}

//	TMNotifyEndOfExecution(pVCpu);

	if (show_debug_message) {
		log(cpu_id, " exit",
		            " last_exit.tsc=", Hex(last_exit.tsc),
		            " last_exit.offset=", Hex(last_exit.offset),
		            " tsc_on_exit.tsc=", Hex(tsc_on_exit.tsc),
		            " tsc_on_exit.offset=", Hex(tsc_on_exit.offset),
		            " sum exit=", Hex(tsc_on_exit.tsc + tsc_on_exit.offset),
		            " sum last=", Hex(last_exit.tsc + last_exit.offset));
		log(cpu_id, "     ",
		            " val_diff=", Hex(tsc_on_exit.tsc - last_exit.tsc),
		            " val+off_diff=", Hex(tsc_on_exit.tsc + tsc_on_exit.offset - last_exit.tsc - last_exit.offset),
		            " (off_diff=", Hex(tsc_on_exit.offset - last_exit.offset), ")");
	}
}
