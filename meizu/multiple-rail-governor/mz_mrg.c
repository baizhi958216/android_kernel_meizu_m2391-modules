// SPDX-License-Identifier: GPL-2.0-only
/* M2391 Multiple Rail Governor. Recovered from the original split BTF,
 * complete function disassembly and the board's master/slave DT rules.
 * Frequencies in graph nodes are Hz; cpufreq requests/tracepoints use kHz.
 */
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <trace/hooks/cpufreq.h>
#include "mz_mrg_internal.h"
#define CREATE_TRACE_POINTS
#include "mz_mrg_trace.h"

static LIST_HEAD(rule_list);
static LIST_HEAD(freq_list);
static DEFINE_MUTEX(lifecycle_lock);
static DEFINE_SPINLOCK(graph_lock);
static struct kthread_worker worker;
static struct task_struct *worker_task;
static cpumask_t highest_cluster;
static unsigned int highest_freq, bypass_freq;
static bool bypass, initialized, stopping;
static struct platform_device *owner;

/* graph_lock serializes hook/notifier access; lifecycle_lock pins graph
 * objects across sleepable QoS updates and policy removal. */
static struct mrg_freq_device *find_policy(struct cpufreq_policy *policy)
{
	struct mrg_freq_device *freq;

	list_for_each_entry(freq, &freq_list, node)
		if (freq->policy == policy)
			return freq;
	return NULL;
}
static void queue_rule(struct mrg_rule *rule)
{
	if (!stopping && worker_task)
		irq_work_queue(&rule->irq_work);
}
static void update_rule(struct mrg_rule *rule, bool force)
{
	rule->state_idx = mrg_rule_state(rule);
	if (force || !rule->coop_work) {
		rule->disable_rule = false;
		queue_rule(rule);
	}
}
static void mrg_irq_work(struct irq_work *irq)
{
	struct mrg_rule *rule = container_of(irq, struct mrg_rule, irq_work);

	if (!READ_ONCE(stopping))
		kthread_queue_work(&worker, rule->coop_work ?: &rule->exec_work);
}
static void mrg_rule_update_work(struct kthread_work *work)
{
	struct mrg_rule *rule = container_of(work, struct mrg_rule, exec_work);
	struct mrg_freq_device *freq;
	unsigned long flags, minimum, maximum;
	bool disabled;
	int i, ret;

	/* Match cpufreq's hotplug lock order: never wait for cpus_read_lock
	 * while holding the mutex that an unregistering policy needs. */
	cpus_read_lock();
	mutex_lock(&lifecycle_lock);
	if (stopping)
		goto unlock;
	spin_lock_irqsave(&graph_lock, flags);
	disabled = rule->disable_rule;
	spin_unlock_irqrestore(&graph_lock, flags);
	for (i = 0; i < rule->slave_num; i++) {
		freq = rule->slave_dev[i].freq_dev;
		if (!freq || !cpumask_intersects(&freq->cpus, cpu_online_mask))
			continue;
		spin_lock_irqsave(&graph_lock, flags);
		if (disabled) {
			minimum = freq->cpuinfo_min_freq;
			maximum = freq->cpuinfo_max_freq;
		} else {
			mrg_slave_range(freq, &minimum, &maximum);
		}
		minimum /= freq->amp;
		maximum /= freq->amp;
		if (maximum < freq->cpuinfo_min_freq / freq->amp ||
		    (!disabled && minimum == freq->prev_min_freq &&
		     maximum == freq->prev_max_freq)) {
			spin_unlock_irqrestore(&graph_lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&graph_lock, flags);
		ret = freq_qos_update_request(&freq->min_qos_req, minimum);
		if (ret < 0)
			pr_err("fail to update min freq for CPU%d ret=%d\n", freq->policy->cpu, ret);
		else
			freq->prev_min_freq = minimum;
		ret = freq_qos_update_request(&freq->max_qos_req, maximum);
		if (ret < 0)
			pr_err("fail to update max freq for CPU%d ret=%d\n", freq->policy->cpu, ret);
		else
			freq->prev_max_freq = maximum;
	}
unlock:
	mutex_unlock(&lifecycle_lock);
	cpus_read_unlock();
}
static int mrg_cpufreq_notifier_max(struct notifier_block *nb,
		unsigned long value, void *unused)
{
	struct mrg_freq_device *freq = container_of(nb, struct mrg_freq_device, nb_max);
	struct mrg_device *master;
	unsigned long flags;
	int state, old;

	trace_mrg_notify_nb(freq->policy->cpu, value);
	spin_lock_irqsave(&graph_lock, flags);
	if (!stopping) {
		list_for_each_entry(master, &freq->master_list, node) {
			state = mrg_divider_state(master, value);
			old = master->state;
			master->state = state;
			if (state != old) {
				update_rule(master->ruler, true);
				trace_mrg_notify_state_change(freq->policy->cpu, state, old);
			}
		}
	}
	spin_unlock_irqrestore(&graph_lock, flags);
	return NOTIFY_DONE;
}
static void match_rule(struct mrg_freq_device *freq, struct mrg_rule *rule)
{
	int i;

	for (i = 0; i < rule->master_num; i++) {
		struct mrg_device *master = &rule->master_dev[i];

		if (master->np == freq->np) {
			master->freq_dev = freq;
			master->state = mrg_divider_state(master, freq->cur_freq);
			list_add_tail(&master->node, &freq->master_list);
			return;
		}
	}
	for (i = 0; i < rule->slave_num; i++) {
		struct mrg_device *slave = &rule->slave_dev[i];

		if (slave->np == freq->np) {
			slave->freq_dev = freq;
			list_add_tail(&slave->node, &freq->slave_list);
			return;
		}
	}
}
static int register_policy(struct cpufreq_policy *policy)
{
	struct mrg_freq_device *freq;
	struct mrg_rule *rule;
	struct device *dev;
	unsigned long flags;
	int ret;

	if (IS_ERR_OR_NULL(policy) || find_policy(policy))
		return 0;
	dev = get_cpu_device(cpumask_first(policy->related_cpus));
	if (!dev || !dev->of_node)
		return -ENODEV;
	freq = kzalloc(sizeof(*freq), GFP_KERNEL);
	if (!freq)
		return -ENOMEM;
	freq->policy = cpufreq_cpu_get(policy->cpu);
	if (!freq->policy) {
		ret = -ENODEV;
		goto free;
	}
	freq->np = of_node_get(dev->of_node);
	cpumask_copy(&freq->cpus, policy->related_cpus);
	cpumask_copy(&freq->cpus_online, policy->related_cpus);
	freq->amp = 1000;
	freq->cur_freq = policy->cur;
	freq->cpuinfo_min_freq = (unsigned long)policy->cpuinfo.min_freq * 1000;
	freq->cpuinfo_max_freq = (unsigned long)policy->cpuinfo.max_freq * 1000;
	freq->prev_max_freq = policy->max;
	freq->prev_min_freq = policy->min;
	INIT_LIST_HEAD(&freq->master_list);
	INIT_LIST_HEAD(&freq->slave_list);
	ret = freq_qos_add_request(&policy->constraints, &freq->min_qos_req,
		FREQ_QOS_MIN, policy->cpuinfo.min_freq);
	if (ret < 0)
		goto put;
	ret = freq_qos_add_request(&policy->constraints, &freq->max_qos_req,
		FREQ_QOS_MAX, policy->cpuinfo.max_freq);
	if (ret < 0)
		goto remove_min;
	freq->nb_max.notifier_call = mrg_cpufreq_notifier_max;
	ret = freq_qos_add_notifier(&policy->constraints, FREQ_QOS_MAX, &freq->nb_max);
	if (ret)
		goto remove_max;
	spin_lock_irqsave(&graph_lock, flags);
	list_add_tail(&freq->node, &freq_list);
	list_for_each_entry(rule, &rule_list, node)
		match_rule(freq, rule);
	spin_unlock_irqrestore(&graph_lock, flags);
	return 0;
remove_max:
	freq_qos_remove_request(&freq->max_qos_req);
remove_min:
	freq_qos_remove_request(&freq->min_qos_req);
put:
	of_node_put(freq->np);
	cpufreq_cpu_put(freq->policy);
free:
	kfree(freq);
	return ret;
}
void mrg_cpufreq_register(struct cpufreq_policy *policy)
{
	mutex_lock(&lifecycle_lock);
	if (owner && !stopping)
		register_policy(policy);
	mutex_unlock(&lifecycle_lock);
}
EXPORT_SYMBOL_GPL(mrg_cpufreq_register);

static void unregister_policy(struct mrg_freq_device *freq)
{
	struct mrg_device *device, *next;
	unsigned long flags;

	spin_lock_irqsave(&graph_lock, flags);
	list_del(&freq->node);
	list_for_each_entry_safe(device, next, &freq->master_list, node) {
		list_del_init(&device->node);
		device->freq_dev = NULL;
		device->state = -1;
		update_rule(device->ruler, false);
	}
	list_for_each_entry_safe(device, next, &freq->slave_list, node) {
		list_del_init(&device->node);
		device->freq_dev = NULL;
		device->state = -1;
	}
	spin_unlock_irqrestore(&graph_lock, flags);
	freq_qos_remove_notifier(&freq->policy->constraints, FREQ_QOS_MAX, &freq->nb_max);
	freq_qos_remove_request(&freq->max_qos_req);
	freq_qos_remove_request(&freq->min_qos_req);
	of_node_put(freq->np);
	cpufreq_cpu_put(freq->policy);
	kfree(freq);
}
void mrg_cpufreq_unregister(struct cpufreq_policy *policy)
{
	struct mrg_freq_device *freq;

	mutex_lock(&lifecycle_lock);
	freq = find_policy(policy);
	if (freq)
		unregister_policy(freq);
	mutex_unlock(&lifecycle_lock);
}
EXPORT_SYMBOL_GPL(mrg_cpufreq_unregister);
/* Caller holds graph_lock; tracepoint callbacks cannot sleep. */
static unsigned long check_limit(struct mrg_freq_device *freq,
		unsigned long target)
{
	struct mrg_device *master;
	unsigned long minimum, maximum, normalized;
	int state;

	if (!initialized) {
		freq->cur_freq = target * freq->amp / 1000;
		return target;
	}
	mrg_slave_range(freq, &minimum, &maximum);
	normalized = min(max(target * freq->amp, minimum), maximum) / 1000;
	list_for_each_entry(master, &freq->master_list, node) {
		state = mrg_divider_state(master, normalized);
		if (state != master->state) {
			master->state = state;
			update_rule(master->ruler, true);
		}
	}
	freq->cur_freq = normalized;
	return normalized * 1000 / freq->amp;
}
unsigned int mrg_cpufreq_check_limit(struct cpufreq_policy *policy,
		unsigned int target_freq)
{
	struct mrg_freq_device *freq;
	unsigned long flags;

	if (IS_ERR_OR_NULL(policy))
		return target_freq;
	spin_lock_irqsave(&graph_lock, flags);
	freq = find_policy(policy);
	if (!stopping && freq && (!list_empty(&freq->master_list) ||
				 !list_empty(&freq->slave_list)))
		target_freq = check_limit(freq, target_freq);
	spin_unlock_irqrestore(&graph_lock, flags);
	return target_freq;
}
EXPORT_SYMBOL_GPL(mrg_cpufreq_check_limit);

static void mrg_cpufreq_freq_update(void *data, struct cpufreq_policy *policy,
		unsigned int *target_freq, unsigned int old_freq)
{
	struct mrg_rule *rule;
	struct mrg_freq_device *freq;
	unsigned long flags;
	bool next;

	if (!policy || !target_freq)
		return;
	spin_lock_irqsave(&graph_lock, flags);
	if (stopping)
		goto unlock;
	if (cpumask_test_cpu(policy->cpu, &highest_cluster)) {
		next = old_freq >= bypass_freq;
		if (next != bypass) {
			bypass = next;
			list_for_each_entry(rule, &rule_list, node) {
				if (next) {
					rule->enable = false;
					rule->disable_rule = true;
					queue_rule(rule);
				} else if (rule->saved_enable) {
					rule->enable = true;
					update_rule(rule, true);
				}
			}
		}
	}
	if (bypass)
		goto unlock;
	*target_freq = old_freq;
	freq = find_policy(policy);
	if (freq && (!list_empty(&freq->master_list) ||
		     !list_empty(&freq->slave_list)))
		*target_freq = check_limit(freq, *target_freq);
	if (policy->freq_table) {
		policy->cached_resolved_idx = cpufreq_frequency_table_target(policy,
			*target_freq, CPUFREQ_RELATION_C);
		policy->cached_target_freq = *target_freq;
	}
unlock:
	spin_unlock_irqrestore(&graph_lock, flags);
	trace_mrg_freq_update(policy->cpu, *target_freq, old_freq);
}

static ssize_t ruler_enable_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buffer)
{
	struct mrg_rule *rule = container_of(kobj, struct mrg_rule, kobj);

	return sysfs_emit(buffer, "%s\n", READ_ONCE(rule->enable) ? "enabled" : "disabled");
}
static ssize_t ruler_enable_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buffer, size_t count)
{
	struct mrg_rule *rule = container_of(kobj, struct mrg_rule, kobj);
	unsigned long flags;
	int enabled;

	if (sscanf(buffer, "%d", &enabled) != 1)
		return -EINVAL;
	spin_lock_irqsave(&graph_lock, flags);
	if (stopping) {
		spin_unlock_irqrestore(&graph_lock, flags);
		return -ENODEV;
	}
	rule->enable = !!enabled;
	rule->saved_enable = !!enabled;
	if (enabled)
		update_rule(rule, true);
	else {
		rule->disable_rule = true;
		queue_rule(rule);
	}
	spin_unlock_irqrestore(&graph_lock, flags);
	return count;
}
static ssize_t ruler_freq_dev_list_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buffer)
{
	struct mrg_rule *rule = container_of(kobj, struct mrg_rule, kobj);
	unsigned long flags;
	ssize_t count = 0;
	int i;

	spin_lock_irqsave(&graph_lock, flags);
	for (i = 0; i < rule->slave_num; i++)
		if (rule->slave_dev[i].freq_dev)
			count += sysfs_emit_at(buffer, count, "slave%d:%s ", i,
				rule->slave_dev[i].freq_dev->np->full_name);
	for (i = 0; i < rule->master_num; i++)
		if (rule->master_dev[i].freq_dev)
			count += sysfs_emit_at(buffer, count, "master%d:%s ", i,
				rule->master_dev[i].freq_dev->np->full_name);
	count += sysfs_emit_at(buffer, count, "\n");
	spin_unlock_irqrestore(&graph_lock, flags);
	return count;
}
static struct kobj_attribute ruler_enable_attr =
	__ATTR(ruler_enable, 0640, ruler_enable_show, ruler_enable_store);
static struct kobj_attribute ruler_devices_attr =
	__ATTR(ruler_freq_dev_list, 0440, ruler_freq_dev_list_show, NULL);
static struct attribute *rule_attrs[] = {
	&ruler_enable_attr.attr, &ruler_devices_attr.attr, NULL,
};
ATTRIBUTE_GROUPS(rule);
static void free_devices(struct mrg_device *devices, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		of_node_put(devices[i].np);
		kfree(devices[i].divider);
	}
	kfree(devices);
}
static void rule_release(struct kobject *kobj)
{
	struct mrg_rule *rule = container_of(kobj, struct mrg_rule, kobj);

	free_devices(rule->master_dev, rule->master_num);
	free_devices(rule->slave_dev, rule->slave_num);
	kfree(rule);
}
static struct kobj_type rule_type = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = rule_release,
	.default_groups = rule_groups,
};
static int parse_devices(struct device_node *node, const char *property,
		struct mrg_rule *rule, struct mrg_device **devices, int *count)
{
	struct of_phandle_args args;
	struct mrg_device *array;
	int cells, i, ret;

	cells = of_property_count_u32_elems(node, property);
	if (cells <= 0 || cells % (rule->divider_num + 1))
		return -EINVAL;
	*count = cells / (rule->divider_num + 1);
	array = kcalloc(*count, sizeof(*array), GFP_KERNEL);
	if (!array) {
		*count = 0;
		return -ENOMEM;
	}
	*devices = array;
	for (i = 0; i < *count; i++) {
		ret = of_parse_phandle_with_fixed_args(node, property,
			rule->divider_num, i, &args);
		if (ret)
			return ret;
		array[i].np = args.np;
		array[i].divider = kmemdup(args.args,
			rule->divider_num * sizeof(u32), GFP_KERNEL);
		if (!array[i].divider)
			return -ENOMEM;
		array[i].ruler = rule;
		array[i].state = -1;
		array[i].max_freq = ULONG_MAX;
		INIT_LIST_HEAD(&array[i].node);
	}
	return 0;
}
static bool devices_subset(const struct mrg_device *small, int nsmall,
		const struct mrg_device *large, int nlarge)
{
	int i, j;

	for (i = 0; i < nsmall; i++) {
		for (j = 0; j < nlarge; j++)
			if (small[i].np == large[j].np)
				break;
		if (j == nlarge)
			return false;
	}
	return true;
}
static int parse_rule(struct device_node *node)
{
	struct mrg_rule *rule, *parent;
	u32 count;
	int ret;

	ret = of_property_read_u32(node, "meizu,divider-cells", &count);
	if (ret)
		return ret;
	if (!count || count > MAX_PHANDLE_ARGS)
		return -EINVAL;
	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return -ENOMEM;
	kobject_init(&rule->kobj, &rule_type);
	rule->name = node->name;
	rule->divider_num = count;
	rule->up_limit = of_property_read_bool(node, "up_limit_enable");
	rule->dn_limit = of_property_read_bool(node, "down_limit_enable");
	rule->thermal_enable = of_property_read_bool(node, "thermal_enable");
	rule->master_vote_max = of_property_read_bool(node, "master_vote_max");
	rule->user_mode_enable = of_property_read_bool(node, "user_mode_enable");
	rule->enable = !rule->user_mode_enable;
	rule->saved_enable = rule->enable;
	rule->state_idx = -1;
	spin_lock_init(&rule->wip_lock);
	kthread_init_work(&rule->exec_work, mrg_rule_update_work);
	init_irq_work(&rule->irq_work, mrg_irq_work);
	ret = parse_devices(node, "meizu,mrg-master", rule,
		&rule->master_dev, &rule->master_num);
	if (ret)
		goto put;
	ret = parse_devices(node, "meizu,mrg-slave", rule,
		&rule->slave_dev, &rule->slave_num);
	if (ret)
		goto put;
	/* A prior rule covering every participating device can execute the
	 * aggregate slave calculation once for both rules. */
	list_for_each_entry(parent, &rule_list, node)
		if (!parent->coop_work &&
		    devices_subset(rule->master_dev, rule->master_num,
			parent->master_dev, parent->master_num) &&
		    devices_subset(rule->slave_dev, rule->slave_num,
			parent->slave_dev, parent->slave_num)) {
			rule->coop_work = &parent->exec_work;
			break;
		}
	list_add_tail(&rule->node, &rule_list);
	return 0;
put:
	kobject_put(&rule->kobj);
	return ret;
}
/* The board's tables are ascending. Recover the predecessor of the greatest
 * OPP without the original table[-1] access for a descending/single-entry
 * table; such a table uses its sole maximum as the bypass threshold. */
static void inspect_highest_policy(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *entry;
	unsigned int maximum = 0, previous = 0;

	if (!policy->freq_table)
		return;
	cpufreq_for_each_valid_entry(entry, policy->freq_table)
		maximum = max(maximum, entry->frequency);
	if (maximum <= highest_freq)
		return;
	cpufreq_for_each_valid_entry(entry, policy->freq_table)
		if (entry->frequency < maximum)
			previous = max(previous, entry->frequency);
	highest_freq = maximum;
	bypass_freq = previous ?: maximum;
	cpumask_copy(&highest_cluster, policy->related_cpus);
}
static void teardown(bool hook_registered)
{
	struct mrg_rule *rule, *next;
	struct mrg_freq_device *freq, *next_freq;
	unsigned long flags;

	spin_lock_irqsave(&graph_lock, flags);
	stopping = true;
	initialized = false;
	spin_unlock_irqrestore(&graph_lock, flags);
	if (hook_registered) {
		unregister_trace_android_vh_cpufreq_fast_switch(mrg_cpufreq_freq_update, NULL);
		tracepoint_synchronize_unregister();
	}
	/* Sysfs, IRQ work and kthread work are drained before graph owners die. */
	list_for_each_entry(rule, &rule_list, node)
		if (rule->kobj.state_in_sysfs)
			kobject_del(&rule->kobj);
	list_for_each_entry(rule, &rule_list, node)
		irq_work_sync(&rule->irq_work);
	if (worker_task)
		kthread_flush_worker(&worker);
	mutex_lock(&lifecycle_lock);
	list_for_each_entry_safe(freq, next_freq, &freq_list, node)
		unregister_policy(freq);
	list_for_each_entry_safe(rule, next, &rule_list, node) {
		list_del(&rule->node);
		kobject_put(&rule->kobj);
	}
	/* The worker has been flushed and all its producers are drained. Keep
	 * the singleton reservation until the old thread is gone, so a new
	 * probe cannot reinitialize its worker underneath kthread_stop(). */
	if (worker_task) {
		kthread_stop(worker_task);
		worker_task = NULL;
	}
	owner = NULL;
	mutex_unlock(&lifecycle_lock);
}
static int mrg_probe(struct platform_device *pdev)
{
	struct device_node *child;
	struct cpufreq_policy *policy;
	struct mrg_rule *rule;
	struct sched_param param = { .sched_priority = 50 };
	unsigned long flags;
	int cpu, ret;
	bool registered = false;

	mutex_lock(&lifecycle_lock);
	if (owner) {
		mutex_unlock(&lifecycle_lock);
		return -EBUSY;
	}
	owner = pdev;
	stopping = false;
	bypass = false;
	initialized = false;
	highest_freq = 0;
	bypass_freq = 0;
	cpumask_clear(&highest_cluster);
	for_each_child_of_node(pdev->dev.of_node, child) {
		ret = parse_rule(child);
		if (ret) {
			of_node_put(child);
			goto fail_locked;
		}
	}
	kthread_init_worker(&worker);
	worker_task = kthread_run(kthread_worker_fn, &worker, "mz-mrg");
	if (IS_ERR(worker_task)) {
		ret = PTR_ERR(worker_task);
		worker_task = NULL;
		goto fail_locked;
	}
	ret = sched_setscheduler_nocheck(worker_task, SCHED_FIFO, &param);
	if (ret)
		goto fail_locked;
	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		if (!find_policy(policy)) {
			ret = register_policy(policy);
			if (!ret)
				inspect_highest_policy(policy);
		} else {
			ret = 0;
		}
		cpufreq_cpu_put(policy);
		if (ret)
			goto fail_locked;
	}
	spin_lock_irqsave(&graph_lock, flags);
	initialized = true;
	list_for_each_entry(rule, &rule_list, node)
		update_rule(rule, false);
	spin_unlock_irqrestore(&graph_lock, flags);
	ret = register_trace_android_vh_cpufreq_fast_switch(mrg_cpufreq_freq_update, NULL);
	if (ret)
		goto fail_locked;
	registered = true;
	list_for_each_entry(rule, &rule_list, node) {
		ret = kobject_add(&rule->kobj, &pdev->dev.kobj, "%s", rule->name);
		if (ret)
			goto fail_locked;
	}
	platform_set_drvdata(pdev, pdev);
	mutex_unlock(&lifecycle_lock);
	return 0;
fail_locked:
	mutex_unlock(&lifecycle_lock);
	teardown(registered);
	return ret;
}
static int mrg_remove(struct platform_device *pdev)
{
	teardown(true);
	platform_set_drvdata(pdev, NULL);
	return 0;
}
static const struct of_device_id mrg_match[] = {
	{ .compatible = "meizu,qcom-mrg" }, {},
};
MODULE_DEVICE_TABLE(of, mrg_match);
static struct platform_driver mrg_driver = {
	.probe = mrg_probe,
	.remove = mrg_remove,
	.driver = { .name = "mz-mrg", .of_match_table = mrg_match },
};
module_platform_driver(mrg_driver);
MODULE_DESCRIPTION("Meizu Multiple Rail Governor Driver");
MODULE_LICENSE("GPL v2");
