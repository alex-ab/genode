/*
 * \brief  Replaces kernel/rcu/srcutree.c.c
 * \author Stefan Kalkowski
 * \date   2021-03-16
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/version.h>

int __srcu_read_lock(struct srcu_struct * ssp)
{
	struct srcu_ctr __percpu *scp = READ_ONCE(ssp->srcu_ctrp);
	this_cpu_inc(scp->srcu_locks.counter);
	return scp - &ssp->sda->srcu_ctrs[0];
}


void __srcu_read_unlock(struct srcu_struct * ssp, int idx) { }


#ifndef CONFIG_DEBUG_LOCK_ALLOC

int init_srcu_struct(struct srcu_struct * ssp)
{
	ssp->srcu_sup = kzalloc(sizeof(*ssp->srcu_sup), GFP_KERNEL);
	if (!ssp->srcu_sup)
		return -ENOMEM;

	mutex_init(&ssp->srcu_sup->srcu_cb_mutex);
	mutex_init(&ssp->srcu_sup->srcu_gp_mutex);
	ssp->srcu_sup->srcu_gp_seq = 0;
	ssp->srcu_sup->srcu_barrier_seq = 0;
	mutex_init(&ssp->srcu_sup->srcu_barrier_mutex);
	atomic_set(&ssp->srcu_sup->srcu_barrier_cpu_cnt, 0);
	/* INIT_DELAYED_WORK(&ssp->work, process_srcu); */
	ssp->sda = alloc_percpu(struct srcu_data);
	ssp->srcu_ctrp = &ssp->sda->srcu_ctrs[0];
	/* init_srcu_struct_nodes(ssp, false); */
	ssp->srcu_sup->srcu_gp_seq_needed_exp = 0;
	ssp->srcu_sup->srcu_last_gp_end = ktime_get_mono_fast_ns();
	smp_store_release(&ssp->srcu_sup->srcu_gp_seq_needed, 0); /* Init done. */
	return ssp->sda ? 0 : -ENOMEM;
}
#endif /* CONFIG_DEBUG_LOCK_ALLOC */
