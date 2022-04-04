/*
 * \brief  Emulation to update jiffies before invoking schedule_timeout().
 *         Schedule_timeout() expects that the jiffie value is current,
 *         in order to setup the timeouts. Without current jiffies,
 *         the programmed timeouts are too short, which leads to timeouts
 *         firing too early. The Intel driver uses this mechanism frequently
 *         by utilizing wait_queue_timeout*() in order to wait for hardware
 *         state changes, e.g. connectors hotplug. The schedule_timeout is
 *         shadowed by the Linker feature '--wrap'. This code can be removed
 *         as soon as the timeout handling is implemented by lx_emul/lx_kit
 *         instead of using the original Linux sources of kernel/time/timer.c.
 * \author Alexander Boettcher
 * \date   2022-04-04
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <linux/clockchips.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/device.h>

/*
 * The functions implemented here are stripped down versions of the Linux
 * internal time handling code, when clock is used in periodic mode.
 *
 * Normally time proceeds due to
 * -> lx_emul/shadow/kernel/sched/core.c calls lx_emul_time_handle()
 *   -> repos/dde_linux/src/lib/lx_emul/clocksource.c:lx_emul_time_hanlde() calls
 *     -> dde_clock_event_device->event_handle() == tick_handle_periodic()
 * -> kernel/time/tick-common.c -> tick_handle_periodic() 
 *   -> kernel/time/clockevents.c -> clockevents_program_event()
 *    -> which fast forwards jiffies in a loop until it matches wall clock
 */


extern int                         timekeeping_valid_for_hres(void);
extern void                        do_timer(unsigned long ticks);
extern struct clock_event_device * dde_clock_event_device;
extern signed long                 __real_schedule_timeout(signed long timeout);


/**
 * based on kernel/time/clockevents.c clockevents_program_event()
 */
static int lx_clockevents_program_event(struct clock_event_device *dev,
                                        ktime_t expires)
{
	int64_t delta;

	if (WARN_ON_ONCE(expires < 0))
		return 0;

	dev->next_event = expires;

	if (clockevent_state_shutdown(dev))
		return 0;

	if (dev->features & CLOCK_EVT_FEAT_KTIME)
		return 0;

	delta = ktime_to_ns(ktime_sub(expires, ktime_get()));
	if (delta <= 0) {
		int res = -ETIME;
		return res;
	}

	return 0;
}


/**
 * based on kernel/time/tick-common.c tick_handle_periodic() 
 */
static void lx_emul_force_jiffies_update(void)
{
	struct clock_event_device *dev = dde_clock_event_device;

	ktime_t next = dev->next_event;

#if defined(CONFIG_HIGH_RES_TIMERS) || defined(CONFIG_NO_HZ_COMMON)
	/*
	 * The cpu might have transitioned to HIGHRES or NOHZ mode via
	 * update_process_times() -> run_local_timers() ->
	 * hrtimer_run_queues().
	 */
	if (dev->event_handler != tick_handle_periodic)
		return;
#endif

	if (!clockevent_state_oneshot(dev))
		return;

	for (;;) {
		/*
		 * Setup the next period for devices, which do not have
		 * periodic mode:
		 */
		next = ktime_add_ns(next, TICK_NSEC);

		if (!lx_clockevents_program_event(dev, next))
			return;

		/*
		 * Have to be careful here. If we're in oneshot mode,
		 * before we call tick_periodic() in a loop, we need
		 * to be sure we're using a real hardware clocksource.
		 * Otherwise we could get trapped in an infinite
		 * loop, as the tick_periodic() increments jiffies,
		 * which then will increment time, possibly causing
		 * the loop to trigger again and again.
		 */
		if (timekeeping_valid_for_hres()) {
			do_timer(1);  /* tick_periodic(cpu); */
		}
	}
}


signed long __wrap_schedule_timeout(signed long timeout)
{
	lx_emul_force_jiffies_update();
	return __real_schedule_timeout(timeout);
}
