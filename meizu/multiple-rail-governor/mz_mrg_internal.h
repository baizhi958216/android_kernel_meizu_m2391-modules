/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_MRG_INTERNAL_H
#define _MEIZU_MRG_INTERNAL_H

#include <linux/cpufreq.h>
#include <linux/irq_work.h>
#include <linux/kthread.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/of.h>

struct mrg_freq_device;
struct mrg_rule;
struct mrg_device {
	struct mrg_freq_device *freq_dev;
	struct device_node *np;
	struct mrg_rule *ruler;
	unsigned int *divider;
	struct list_head node;
	unsigned long min_freq;
	unsigned long max_freq;
	int state;
	bool idle;
};
struct mrg_rule {
	struct kobject kobj;
	struct kthread_work exec_work;
	struct kthread_work *coop_work;
	bool work_in_progress;
	struct irq_work irq_work;
	spinlock_t wip_lock;
	struct list_head node;
	struct mrg_device *master_dev;
	struct mrg_device *slave_dev;
	const char *name;
	int divider_num;
	int master_num;
	int slave_num;
	int state_idx;
	bool dn_limit;
	bool up_limit;
	bool thermal_enable;
	bool master_vote_max;
	bool user_mode_enable;
	bool enable;
	bool saved_enable;
	bool disable_rule;
};
struct mrg_freq_device {
	cpumask_t cpus;
	cpumask_t cpus_online;
	struct devfreq *df;
	struct device_node *np;
	struct kthread_work destroy_work;
	struct list_head master_list;
	struct list_head slave_list;
	struct list_head node;
	unsigned int amp;
	unsigned long cur_freq;
	unsigned long cpuinfo_min_freq;
	unsigned long cpuinfo_max_freq;
	unsigned long prev_max_freq;
	unsigned long prev_min_freq;
	struct freq_qos_request min_qos_req;
	struct freq_qos_request max_qos_req;
	struct notifier_block nb_max;
	struct cpufreq_policy *policy;
};

/* Original private layouts are evidence, not an externally shared ABI. */
static_assert(sizeof(struct mrg_device) == 72);
static_assert(sizeof(struct mrg_rule) == 248);
static_assert(sizeof(struct mrg_freq_device) == 344);
static_assert(sizeof(struct cpufreq_policy) == 760);
static_assert(offsetof(struct cpufreq_policy, cpus) == 0);
static_assert(offsetof(struct cpufreq_policy, related_cpus) == 8);
static_assert(offsetof(struct cpufreq_policy, cpu) == 28);
static_assert(offsetof(struct cpufreq_policy, cpuinfo) == 40);
static_assert(offsetof(struct cpufreq_policy, min) == 52);
static_assert(offsetof(struct cpufreq_policy, max) == 56);
static_assert(offsetof(struct cpufreq_policy, cur) == 60);
static_assert(offsetof(struct cpufreq_policy, constraints) == 160);
static_assert(offsetof(struct cpufreq_policy, freq_table) == 400);
static_assert(offsetof(struct cpufreq_policy, freq_table_sorted) == 408);
static_assert(offsetof(struct cpufreq_policy, cached_target_freq) == 636);
static_assert(offsetof(struct cpufreq_policy, cached_resolved_idx) == 640);
static_assert(sizeof(struct cpufreq_frequency_table) == 12);
static_assert(offsetof(struct cpufreq_frequency_table, frequency) == 8);


/* A rule votes with its least-demanding master unless explicitly configured
 * master_vote_max. An idle master votes zero; uninitialized masters abstain.
 */
static inline int mrg_rule_state(const struct mrg_rule *rule)
{
	int i, vote = -1;

	if (!READ_ONCE(rule->enable))
		return -1;
	for (i = 0; i < rule->master_num; i++) {
		const struct mrg_device *master = &rule->master_dev[i];
		int state = READ_ONCE(master->idle) ? 0 : READ_ONCE(master->state);

		if (state < 0)
			continue;
		if (vote < 0 || (rule->master_vote_max ? state > vote : state < vote))
			vote = state;
	}
	return vote;
}
static inline int mrg_divider_state(const struct mrg_device *device,
		unsigned long freq)
{
	int i, state = 0;

	for (i = 0; i < device->ruler->divider_num; i++)
		if (freq >= device->divider[i])
			state++;
	return state;
}
/* Bounds use Hz internally, including the physical policy limits.  The
 * original intersects each rule after clamping that rule to the device range.
 */
static inline void mrg_slave_range(struct mrg_freq_device *freq_dev,
		unsigned long *minimum, unsigned long *maximum)
{
	struct mrg_device *slave;

	*minimum = 0;
	*maximum = ULONG_MAX;
	list_for_each_entry(slave, &freq_dev->slave_list, node) {
		struct mrg_rule *rule = slave->ruler;
		unsigned long lo = 0, hi = ULONG_MAX;
		int state = mrg_rule_state(rule);

		if (state >= 1) {
			if (rule->dn_limit)
				lo = (unsigned long)slave->divider[state - 1] * 1000;
			if (rule->up_limit)
				hi = (unsigned long)slave->divider[state - 1] * 1000;
		}
		slave->min_freq = max(lo, freq_dev->cpuinfo_min_freq);
		slave->max_freq = min(hi, freq_dev->cpuinfo_max_freq);
		*maximum = min(max(slave->max_freq, *minimum), *maximum);
		*minimum = min(max(slave->min_freq, *minimum), *maximum);
	}
}
#endif
