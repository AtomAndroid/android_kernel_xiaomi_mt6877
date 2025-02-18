// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/err.h>
#include <linux/rcupdate.h>
#include "tuning.h"

static DEFINE_MUTEX(boost_mutex);

#ifdef CONFIG_MTK_SCHED_CPU_PREFER
int sched_set_cpuprefer(pid_t pid, unsigned int prefer_type)
{
	struct task_struct *p;
	unsigned long flags;
	int retval = 0;

	if (!valid_cpu_prefer(prefer_type) || pid < 0)
		return -EINVAL;

	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (p != NULL) {
		raw_spin_lock_irqsave(&p->pi_lock, flags);
		p->cpu_prefer = prefer_type;
		raw_spin_unlock_irqrestore(&p->pi_lock, flags);
		trace_sched_set_cpuprefer(p);
	} else {
		retval = -ESRCH;
	}
	rcu_read_unlock();

	return retval;
}
EXPORT_SYMBOL(sched_set_cpuprefer);
#endif

#ifdef CONFIG_MTK_SCHED_BIG_TASK_MIGRATE
void set_sched_rotation_enable(bool enable)
{
	big_task_rotation_enable = enable;
}
EXPORT_SYMBOL(set_sched_rotation_enable);
#endif /* CONFIG_MTK_SCHED_BIG_TASK_MIGRATE */

#if defined(CONFIG_CPUSETS) && defined(CONFIG_MTK_SCHED_CPU_PREFER)
enum {
	SCHED_NO_BOOST = 0,
	SCHED_ALL_BOOST,
};

/*global variable for recording customer's setting type*/
static int sched_boost_type = SCHED_NO_BOOST;

int get_task_group_path(struct task_group *tg, char *buf, size_t buf_len)
{
	return cgroup_path(tg->css.cgroup, buf, buf_len);
}

static void boost_kick_cpus(void)
{
	int i;
	struct cpumask kick_mask;
	u32 nr_running;

	if (sched_boost_type == SCHED_NO_BOOST)
		return;

	mutex_lock(&boost_mutex);

	cpumask_andnot(&kick_mask, cpu_online_mask, cpu_isolated_mask);

	for_each_cpu(i, &kick_mask) {
		/*
		 * kick only "small" cluster
		 */
		if (!is_max_capacity_cpu(i)) {
			nr_running = READ_ONCE(cpu_rq(i)->nr_running);

			/*
			 * make sense to interrupt CPU if its run-queue
			 * has something running in order to check for
			 * migration afterwards, otherwise skip it.
			 */
			if (nr_running >= 1)
				smp_send_reschedule(i);
		}
	}

	mutex_unlock(&boost_mutex);
}

/*
 * set sched boost type
 * @type: reference sched boost type
 * @return :success current type,else return -1
 */
int set_sched_boost_type(int type)
{
	int old_type = sched_boost_type;

	if (type < SCHED_NO_BOOST || type > SCHED_ALL_BOOST) {
		pr_info("Sched boost type should between %d-%d but your valuse is %d\n",
		       SCHED_NO_BOOST, SCHED_ALL_BOOST, type);
		return -1;
	}

	sched_boost_type = type;

	/*
	 * Kick low capacity CPUs to force immediate migration of tasks
	 * onto high capcity CPUs.
	 */
	if (old_type != sched_boost_type)
		boost_kick_cpus();

	return sched_boost_type;
}
EXPORT_SYMBOL(set_sched_boost_type);

int get_sched_boost_type(void)
{
	return sched_boost_type;
}
EXPORT_SYMBOL(get_sched_boost_type);

/*
 * get orig cpu prefer of task
 */
inline int task_orig_cpu_prefer(struct task_struct *task)
{
	return task->cpu_prefer;
}
/*
 * modify task's boost type
 * first priority is SCHED_ALL_BOOST.
 * priority: task < group < all_boost
 */
int cpu_prefer(struct task_struct *task)
{
	int cpu_prefer = task_orig_cpu_prefer(task);
	int cs_prefer = task_cs_cpu_perfer(task);

	if (sched_boost_type == SCHED_ALL_BOOST)
		return SCHED_PREFER_BIG;

	if (cpu_prefer == SCHED_PREFER_LITTLE &&
		uclamp_boosted(task))
		cpu_prefer = SCHED_PREFER_NONE;

	if (cs_prefer > SCHED_PREFER_NONE && cs_prefer < SCHED_PREFER_END)
		cpu_prefer = cs_prefer;

	return cpu_prefer;
}
EXPORT_SYMBOL(cpu_prefer);

#else

int set_sched_boost_type(int type)
{
	return -1;
}
EXPORT_SYMBOL(set_sched_boost_type);

int get_sched_boost_type(void)
{
	return 0;
}
EXPORT_SYMBOL(get_sched_boost_type);

#if defined(CONFIG_MTK_SCHED_CPU_PREFER)

/*check task's boost type*/
inline int cpu_prefer(struct task_struct *task)
{
	int cpu_prefer = task->cpu_prefer;

	if (cpu_prefer == SCHED_PREFER_LITTLE &&
		uclamp_boosted(task))
		cpu_prefer = SCHED_PREFER_NONE;
	}
	return cpu_prefer;
}
#else
/*check task's boost type*/
inline int cpu_prefer(struct task_struct *task)
{
	return 0;
}
#endif
EXPORT_SYMBOL(cpu_prefer);
#endif

void set_capacity_margin(unsigned int margin)
{
	if (margin >= SCHED_CAPACITY_SCALE)
		capacity_margin = margin;
}
EXPORT_SYMBOL(set_capacity_margin);

unsigned int get_capacity_margin(void)
{
	return capacity_margin;
}
EXPORT_SYMBOL(get_capacity_margin);
