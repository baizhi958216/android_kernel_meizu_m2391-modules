// SPDX-License-Identifier: GPL-2.0-only
/* Recovered M2391 asynchronous slab reclaim policy.  The stock daemon uses a
 * single outstanding request; eligible callers bypass synchronous reclaim even
 * when that request is busy.  Keep that policy, while owning the memcg reference
 * and draining the hook, proc callbacks and daemon on teardown.
 */
#include <linux/cpufreq.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/memcontrol.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <linux/swap.h>
#include <trace/hooks/vmscan.h>

extern unsigned long shrink_slab(gfp_t gfp_mask, int nid,
				struct mem_cgroup *memcg, int priority);

static DEFINE_SPINLOCK(request_lock);
static DECLARE_WAIT_QUEUE_HEAD(request_wait);
static struct task_struct *slabd;
static struct proc_dir_entry *proc_dir;
static bool opt_enabled = true;
static bool ready;
static bool busy;
static bool affinity_set;
static struct {
	struct mem_cgroup *memcg;
	gfp_t gfp;
	int nid;
	int priority;
} request;

static void should_shrink_async(void *unused, gfp_t gfp, int nid,
		struct mem_cgroup *memcg, int priority, bool *bypass)
{
	unsigned long flags;

	*bypass = false;
	if (!READ_ONCE(opt_enabled) || !READ_ONCE(ready) ||
	    (current->flags & PF_KSWAPD))
		return;
	spin_lock_irqsave(&request_lock, flags);
	if (!ready)
		goto unlock;
	if (busy) {
		*bypass = true;
		goto unlock;
	}
	if (memcg && !css_tryget_online(&memcg->css))
		goto unlock;
	request.memcg = memcg;
	request.gfp = gfp;
	request.nid = nid;
	request.priority = priority;
	smp_store_release(&busy, true);
	*bypass = true;
unlock:
	spin_unlock_irqrestore(&request_lock, flags);
	if (*bypass)
		wake_up_interruptible(&request_wait);
}

static void set_slabd_cpus(void)
{
	struct cpufreq_policy *policy;
	cpumask_t excluded, allowed;
	unsigned int largest = 0;
	int cpu;

	cpumask_clear(&excluded);
	cpus_read_lock();
	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		/* Original policy offset 40 is cpuinfo.max_freq. */
		if (policy->cpuinfo.max_freq >= largest) {
			largest = policy->cpuinfo.max_freq;
			cpumask_copy(&excluded, policy->related_cpus);
		}
		cpufreq_cpu_put(policy);
	}
	cpumask_andnot(&allowed, cpu_online_mask, &excluded);
	if (!cpumask_empty(&allowed) &&
	    !set_cpus_allowed_ptr(current, &allowed))
		affinity_set = true;
	cpus_read_unlock();
}

static int slabd_thread(void *unused)
{
	struct reclaim_state reclaim_state = {};
	unsigned long flags;

	current->flags |= PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD;
	current->reclaim_state = &reclaim_state;
	set_freezable();
	while (!kthread_freezable_should_stop(NULL)) {
		wait_event_interruptible(request_wait,
			smp_load_acquire(&busy) || kthread_should_stop());
		if (kthread_should_stop())
			break;
		if (!smp_load_acquire(&busy))
			continue;
		if (!affinity_set)
			set_slabd_cpus();
		shrink_slab(request.gfp, request.nid, request.memcg,
			    request.priority);
		spin_lock_irqsave(&request_lock, flags);
		if (request.memcg)
			css_put(&request.memcg->css);
		request.memcg = NULL;
		WRITE_ONCE(busy, false);
		spin_unlock_irqrestore(&request_lock, flags);
	}
	/* A stop may race the final accepted request. */
	if (request.memcg)
		css_put(&request.memcg->css);
	request.memcg = NULL;
	current->reclaim_state = NULL;
	current->flags &= ~(PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD);
	return 0;
}

static ssize_t enabled_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	char text[30];
	int len = scnprintf(text, sizeof(text), "%s\n",
			   READ_ONCE(opt_enabled) ? "enabled" : "disabed");

	return simple_read_from_buffer(buf, count, pos, text, len);
}

static ssize_t enabled_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	char text[20] = {};
	size_t copied = min(count, sizeof(text) - 1);

	if (copy_from_user(text, buf, copied))
		return -EFAULT;
	WRITE_ONCE(opt_enabled, text[0] != '0');
	pr_info("proc_opt_enabled_write opt_enabled %d\n", READ_ONCE(opt_enabled));
	return copied;
}

static const struct proc_ops enabled_ops = {
	.proc_read = enabled_read,
	.proc_write = enabled_write,
};

static int __init slabd_init(void)
{
	int ret;

	slabd = kthread_run(slabd_thread, NULL, "kshrink_slabd");
	if (IS_ERR(slabd))
		return PTR_ERR(slabd);
	proc_dir = proc_mkdir("mz_slabd_opt", NULL);
	if (!proc_dir || !proc_create("enabled", 0666, proc_dir, &enabled_ops)) {
		ret = -ENOMEM;
		goto stop;
	}
	WRITE_ONCE(ready, true);
	ret = register_trace_android_vh_shrink_slab_bypass(should_shrink_async, NULL);
	if (!ret)
		return 0;
	WRITE_ONCE(ready, false);
stop:
	proc_remove(proc_dir);
	kthread_stop(slabd);
	return ret;
}

static void __exit slabd_exit(void)
{
	unsigned long flags;

	spin_lock_irqsave(&request_lock, flags);
	ready = false;
	spin_unlock_irqrestore(&request_lock, flags);
	unregister_trace_android_vh_shrink_slab_bypass(should_shrink_async, NULL);
	tracepoint_synchronize_unregister();
	proc_remove(proc_dir);
	kthread_stop(slabd);
}
module_init(slabd_init);
module_exit(slabd_exit);
MODULE_DESCRIPTION("Meizu asynchronous slab reclaim");
MODULE_LICENSE("GPL");
