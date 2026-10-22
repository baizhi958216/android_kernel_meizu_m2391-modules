// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[SMARTSWAP]" fmt

#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/pagemap.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/vmscan.h>
#include <linux/genhd.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/meizu_smartswap.h>
#include <linux/meizu_vip_task.h>

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "internal.h"
#include "smartswap.h"

static const char *swapd_text[NR_EVENT_ITEMS] = {
#ifdef CONFIG_SMARTSWAP_SWAPD
	"swapd_wakeup",
	"swapd_hit_refaults",
	"swapd_medium_press",
	"swapd_critical_press",
	"swapd_memcg_ratio_skip",
	"swapd_memcg_refault_skip",
	"swapd_shrink_anon",
	"swapd_swapout",
	"swapd_skip_swapout",
	"swapd_empty_round",
	"swapd_over_min_buffer_skip_times",
	"swapd_empty_round_skip_times",
	"swapd_snapshot_times",
	"swapd_skip_shrink_of_window",
	"swapd_manual_pause",
#ifdef CONFIG_OPLUS_JANK
	"swapd_cpu_busy_skip_times",
	"swapd_cpu_busy_break_times",
#endif
#endif
};

enum scan_balance {
	SCAN_EQUAL,
	SCAN_FRACT,
	SCAN_ANON,
	SCAN_FILE,
};

static int log_level = HYB_MAX;
static struct kmem_cache *smartswap_cache;
/* Private ownership records also cover offline memcgs absent from score_head. */
struct meizu_memcg_cache_entry {
	struct list_head node;
	bool online_css_ref;
	bool memcg_released;
	memcg_hybs_t hybs;
};
static LIST_HEAD(meizu_cache_entries);
static LIST_HEAD(meizu_detached_cache_entries);
static DECLARE_WAIT_QUEUE_HEAD(meizu_cache_wait);
static DEFINE_SPINLOCK(meizu_cache_lock);
static atomic_t meizu_cache_count = ATOMIC_INIT(0);
static bool meizu_free_hook_registered;

static struct list_head score_head;
static DEFINE_SPINLOCK(score_list_lock);
DEFINE_MUTEX(smartswap_enable_lock);
static bool smartswap_enabled = false;

DEFINE_MUTEX(reclaim_para_lock);
DEFINE_PER_CPU(struct swapd_event_state, swapd_event_states);

extern unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *memcg,
		unsigned long nr_pages,
		gfp_t gfp_mask,
		bool may_swap);


void smartswap_loglevel_set(int level)
{
	log_level = level;
}

int smartswap_loglevel(void)
{
	return log_level;
}

void __put_memcg_cache(memcg_hybs_t *hybs)
{
	struct meizu_memcg_cache_entry *entry =
		container_of(hybs, struct meizu_memcg_cache_entry, hybs);
	unsigned long flags;

	spin_lock_irqsave(&meizu_cache_lock, flags);
	list_del_init(&entry->node);
	spin_unlock_irqrestore(&meizu_cache_lock, flags);
	kmem_cache_free(smartswap_cache, entry);
	if (atomic_dec_and_test(&meizu_cache_count))
		wake_up_all(&meizu_cache_wait);
}

static inline void sum_smartswap_vm_events(unsigned long *ret)
{
	int cpu;
	int i;

	memset(ret, 0, NR_EVENT_ITEMS * sizeof(unsigned long));

	for_each_online_cpu(cpu) {
		struct swapd_event_state *this =
			&per_cpu(swapd_event_states, cpu);

		for (i = 0; i < NR_EVENT_ITEMS; i++)
			ret[i] += this->event[i];
	}
}

static inline void all_smartswap_vm_events(unsigned long *ret)
{
	cpus_read_lock();
	sum_smartswap_vm_events(ret);
	cpus_read_unlock();
}

ssize_t smartswap_vmstat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned long *vm_buf = NULL;
	int len = 0;
	int i = 0;

	vm_buf = kzalloc(sizeof(struct swapd_event_state), GFP_KERNEL);
	if (!vm_buf)
		return -ENOMEM;
	all_smartswap_vm_events(vm_buf);

#ifdef CONFIG_SMARTSWAP_SWAPD
	len += snprintf(buf + len, PAGE_SIZE - len, "%-32s %12lu\n",
			"fault_out_pause", atomic_long_read(&fault_out_pause));
	len += snprintf(buf + len, PAGE_SIZE - len, "%-32s %12lu\n",
			"fault_out_pause_cnt", atomic_long_read(&fault_out_pause_cnt));
#endif

	for (;i < NR_EVENT_ITEMS; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%-32s %12lu\n",
				swapd_text[i], vm_buf[i]);
		if (len == PAGE_SIZE)
			break;
	}
	kfree(vm_buf);

	return len;
}

ssize_t smartswap_loglevel_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	char *type_buf = NULL;
	unsigned long val;

	type_buf = strstrip((char *)buf);
	if (kstrtoul(type_buf, 0, &val))
		return -EINVAL;

	if (val >= HYB_MAX) {
		log_err("val %lu is not valid\n", val);
		return -EINVAL;
	}
	smartswap_loglevel_set((int)val);

	return len;
}

ssize_t smartswap_loglevel_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t size = 0;

	size += scnprintf(buf + size, PAGE_SIZE - size,
			"Smartswap log level: %d\n", smartswap_loglevel());

	return size;
}

/* Make sure the memcg is not NULL in caller */
memcg_hybs_t *smartswap_cache_alloc(struct mem_cgroup *memcg, bool atomic)
{
	memcg_hybs_t *hybs;
	struct meizu_memcg_cache_entry *entry;
	unsigned long irqflags;
	u64 ret;
	gfp_t flags = GFP_KERNEL;

	/* Concurrent cgroup writers may both observe a previously absent cache. */
	hybs = (memcg_hybs_t *)READ_ONCE(memcg->android_oem_data1[0]);
	if (hybs)
		return hybs;

	if (atomic)
		flags &= ~__GFP_DIRECT_RECLAIM;

	entry = kmem_cache_zalloc(smartswap_cache, flags);
	if (!entry)
		return NULL;
	INIT_LIST_HEAD(&entry->node);
	hybs = &entry->hybs;
	atomic_inc(&meizu_cache_count);

	INIT_LIST_HEAD(&hybs->score_node);
#ifdef CONFIG_SMARTSWAP_CORE
	spin_lock_init(&hybs->zram_init_lock);
#endif
	atomic64_set(&hybs->app_score, 300);
	atomic64_set(&hybs->ub_ufs2zram_ratio, 100);
#ifdef CONFIG_SMARTSWAP_SWAPD
	atomic_set(&hybs->ub_mem2zram_ratio, 80);
	atomic_set(&hybs->ub_zram2ufs_ratio, 50);
	atomic_set(&hybs->refault_threshold, 50);
#endif
	hybs->memcg = memcg;
	refcount_set(&hybs->usage, 1);

	spin_lock_irqsave(&meizu_cache_lock, irqflags);
	ret = atomic64_cmpxchg((atomic64_t *)&memcg->android_oem_data1[0], 0, (u64)hybs);
	if (!ret)
		list_add_tail(&entry->node, &meizu_cache_entries);
	spin_unlock_irqrestore(&meizu_cache_lock, irqflags);
	if (ret != 0) {
		put_memcg_cache(hybs);
		return (memcg_hybs_t *)ret;
	}

	return hybs;
}

#ifdef CONFIG_SMARTSWAP_SWAPD
static atomic64_t meizu_scan_round = ATOMIC64_INIT(0);

static void tune_scan_type_hook(void *data, char *scan_balance)
{
	/* Original matches the full worker prefix before checking core enable. */
	if (!strncmp(current->comm, "smartswapd:", 11)) {
		u32 ratio = READ_ONCE(smartswap_shrink_af_ratio);

		*scan_balance = SCAN_ANON;
		if (ratio) {
			u64 round = (u64)atomic64_inc_return(&meizu_scan_round);

			if (!(round % ((u64)ratio + 1)))
				*scan_balance = SCAN_FILE;
		}
		return;
	}
#ifdef CONFIG_SMARTSWAP_CORE
	if (smartswap_core_enabled()) {
		struct zram *zram;
		unsigned long pages, same, increase, total, limit;
		int idx;

		zram = smartswap_owner_get(&idx);
		if (!zram)
			return;
		if (smartswap_swapd_enabled()) {
			pages = atomic64_read(&zram->stats.pages_stored);
			same = atomic64_read(&zram->stats.same_pages);
			increase = READ_ONCE(zram->increase_nr_pages) >> 1;
			total = (READ_ONCE(zram->disksize) >> PAGE_SHIFT) - increase;
			total = total ?: 1;
			limit = total - (total >> 6);
			/* Stock distinguishes compressed/non-same pressure from
			 * total stored pages and applies the half-reservation twice.
			 */
			if (pages - same > limit - increase || pages >= limit)
				*scan_balance = SCAN_FILE;
		}
		smartswap_owner_put(idx);
	}
#endif
}
#endif

static void mem_cgroup_alloc_hook(void *data, struct mem_cgroup *memcg)
{
	if (memcg->android_oem_data1[0])
		BUG();

	smartswap_cache_alloc(memcg, true);
}

static void mem_cgroup_free_hook(void *data, struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs;
	unsigned long flags;

	spin_lock_irqsave(&meizu_cache_lock, flags);
	hybs = (memcg_hybs_t *)xchg(&memcg->android_oem_data1[0], 0);
	if (hybs) {
		struct meizu_memcg_cache_entry *entry =
			container_of(hybs, struct meizu_memcg_cache_entry, hybs);

		entry->memcg_released = true;
		list_move_tail(&entry->node, &meizu_detached_cache_entries);
	}
	spin_unlock_irqrestore(&meizu_cache_lock, flags);
	if (hybs)
		put_memcg_cache(hybs);
}

void memcg_app_score_update(struct mem_cgroup *target)
{
	struct list_head *pos = NULL;
	unsigned long flags;

#ifdef CONFIG_SMARTSWAP_SWAPD
	update_swapd_memcg_param(target);
#endif
	spin_lock_irqsave(&score_list_lock, flags);
	list_for_each(pos, &score_head) {
		memcg_hybs_t *hybs = list_entry(pos, memcg_hybs_t, score_node);
		if (atomic64_read(&hybs->app_score) <
				atomic64_read(&MEMCGRP_ITEM(target, app_score)))
			break;
	}
	list_move_tail(&MEMCGRP_ITEM(target, score_node), pos);
	spin_unlock_irqrestore(&score_list_lock, flags);
}

static void meizu_cache_pin_css(memcg_hybs_t *hybs,
		struct cgroup_subsys_state *css, bool lazy)
{
	struct meizu_memcg_cache_entry *entry =
		container_of(hybs, struct meizu_memcg_cache_entry, hybs);
	unsigned long flags;

	spin_lock_irqsave(&meizu_cache_lock, flags);
	if (!entry->online_css_ref) {
		if (!lazy) {
			/* The online hook runs before core sets CSS_ONLINE. */
			css_get(css);
			entry->online_css_ref = true;
		} else if ((READ_ONCE(css->flags) & CSS_ONLINE) &&
				css_tryget_online(css)) {
			/* A dying CSS must not acquire a ref after its offline hook. */
			entry->online_css_ref = true;
		}
	}
	spin_unlock_irqrestore(&meizu_cache_lock, flags);
}

static void mem_cgroup_css_online_hook(void *data,
		struct cgroup_subsys_state *css, struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);

	if (!hybs)
		return;
	memcg_app_score_update(memcg);
	meizu_cache_pin_css(hybs, css, false);
}

static void mem_cgroup_css_offline_hook(void *data,
		struct cgroup_subsys_state *css, struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);
	unsigned long flags;
	bool put = false;

	if (!hybs)
		return;
	spin_lock_irqsave(&score_list_lock, flags);
	list_del_init(&hybs->score_node);
	spin_unlock_irqrestore(&score_list_lock, flags);
	spin_lock_irqsave(&meizu_cache_lock, flags);
	{
		struct meizu_memcg_cache_entry *entry =
			container_of(hybs, struct meizu_memcg_cache_entry, hybs);

		put = entry->online_css_ref;
		entry->online_css_ref = false;
	}
	spin_unlock_irqrestore(&meizu_cache_lock, flags);
	if (put)
		css_put(css);
}

#define REGISTER_HOOK(name) do {\
	rc = register_trace_android_vh_##name(name##_hook, NULL);\
	if (rc) {\
		log_err("%s:%d register hook %s failed", __FILE__, __LINE__, #name);\
		goto err_out_##name;\
	}\
} while (0)

#define UNREGISTER_HOOK(name) do {\
	unregister_trace_android_vh_##name(name##_hook, NULL);\
} while (0)

#define ERROR_OUT(name) err_out_##name

/* Original M2391 policy leaves kswapd unchanged and directs only swapd
 * toward anonymous pages; other direct reclaim avoids competing with it.
 */
static void smartzram_tune_swappiness_hook(void *unused, int *swappiness)
{
	if (!smartswap_core_enabled() || (current->flags & PF_KSWAPD))
		return;
	*swappiness = !strncmp(current->comm, "smartswapd", 10) ? 200 : 0;
}

#ifdef CONFIG_SMARTSWAP_CORE
static void show_mem_hook(void *unused, unsigned int filter, nodemask_t *nodemask)
{
	unsigned long total, used;

	if (!smartswap_core_enabled())
		return;
	if (smartswap_stored_info(&total, &used))
		return;
	printk(KERN_INFO "[SMARTSWAP]EST: %llu KB\n",
			(unsigned long long)total << (PAGE_SHIFT - 10));
	printk(KERN_INFO "[SMARTSWAP]ESU_C: %llu KB\n",
			(unsigned long long)used << (PAGE_SHIFT - 10));
}
#endif

static int register_all_hooks(void)
{
	int rc;

	/* Keep the final-free observer alive through all producer rollback. */
	REGISTER_HOOK(mem_cgroup_free);
	meizu_free_hook_registered = true;
	REGISTER_HOOK(mem_cgroup_alloc);
	/* mem_cgroup_css_online_hook */
	REGISTER_HOOK(mem_cgroup_css_online);
	/* mem_cgroup_css_offline_hook */
	REGISTER_HOOK(mem_cgroup_css_offline);
#ifdef CONFIG_SMARTSWAP_SWAPD
	/* Wake only after slowpath allocation, as the original module does. */
	REGISTER_HOOK(alloc_pages_slowpath);
	/* tune_scan_type_hook */
	REGISTER_HOOK(tune_scan_type);
#endif
#ifdef CONFIG_SMARTSWAP_CORE
	/* mem_cgroup_id_remove_hook */
	REGISTER_HOOK(mem_cgroup_id_remove);
	REGISTER_HOOK(smartzram_tune_swappiness);
	REGISTER_HOOK(show_mem);
#endif
	return 0;

#ifdef CONFIG_SMARTSWAP_CORE
ERROR_OUT(show_mem):
	UNREGISTER_HOOK(smartzram_tune_swappiness);
ERROR_OUT(smartzram_tune_swappiness):
	UNREGISTER_HOOK(mem_cgroup_id_remove);
ERROR_OUT(mem_cgroup_id_remove):
#endif
#ifdef CONFIG_SMARTSWAP_SWAPD
	UNREGISTER_HOOK(tune_scan_type);
ERROR_OUT(tune_scan_type):
	UNREGISTER_HOOK(alloc_pages_slowpath);
ERROR_OUT(alloc_pages_slowpath):
#endif
	UNREGISTER_HOOK(mem_cgroup_css_offline);
ERROR_OUT(mem_cgroup_css_offline):
	UNREGISTER_HOOK(mem_cgroup_css_online);
ERROR_OUT(mem_cgroup_css_online):
	UNREGISTER_HOOK(mem_cgroup_alloc);
ERROR_OUT(mem_cgroup_alloc):
ERROR_OUT(mem_cgroup_free):
	return rc;
}

static void unregister_all_hook(void)
{
	UNREGISTER_HOOK(mem_cgroup_alloc);
	/* Free stays registered until registry detachment below. */
	UNREGISTER_HOOK(mem_cgroup_css_offline);
	UNREGISTER_HOOK(mem_cgroup_css_online);
#ifdef CONFIG_SMARTSWAP_CORE
	UNREGISTER_HOOK(show_mem);
	UNREGISTER_HOOK(smartzram_tune_swappiness);
	UNREGISTER_HOOK(mem_cgroup_id_remove);
#endif
#ifdef CONFIG_SMARTSWAP_SWAPD
	UNREGISTER_HOOK(alloc_pages_slowpath);
	UNREGISTER_HOOK(tune_scan_type);
#endif
}

static unsigned long memcg_page_state_local(struct mem_cgroup *memcg, int idx)
{
	long value = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		value += per_cpu(memcg->vmstats_percpu->state[idx], cpu);
	return max(value, 0L);
}

unsigned long memcg_anon_pages(struct mem_cgroup *memcg)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	struct lruvec *lruvec = NULL;
	struct mem_cgroup_per_node *mz = NULL;
#endif
	if (!memcg)
		return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	mz = mem_cgroup_nodeinfo(memcg, 0);
	if (!mz)
		return 0;

	lruvec = &mz->lruvec;
	if (!lruvec)
		return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	return (mem_cgroup_get_lru_size(lruvec, LRU_ACTIVE_ANON) +
			mem_cgroup_get_lru_size(lruvec, LRU_INACTIVE_ANON));
#else
	return (lruvec_page_state(lruvec, NR_ACTIVE_ANON) +
			lruvec_page_state(lruvec, NR_INACTIVE_ANON));
#endif
#else
	return (memcg_page_state_local(memcg, NR_ACTIVE_ANON) +
			memcg_page_state_local(memcg, NR_INACTIVE_ANON));
#endif
}

static ssize_t mem_cgroup_force_shrink_anon(struct kernfs_open_file *of,
		char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	struct lruvec *lruvec;
	unsigned long mode, target, reclaimed, done;
	int active;

	if (kstrtoul(strim(buf), 0, &mode))
		return -EINVAL;
	if (!memcg || !MEMCGRP_ITEM_DATA(memcg))
		return -EINVAL;
	if (mode > 1)
		return nbytes;
	lruvec = mem_cgroup_lruvec(memcg, NODE_DATA(0));
	/* Stock mode 1 drains active first, then takes a fresh inactive snapshot. */
	for (active = mode; active >= 0; active--) {
		target = lruvec_page_state(lruvec,
				active ? NR_ACTIVE_ANON : NR_INACTIVE_ANON);
		for (done = 0; done < target; done += reclaimed) {
			reclaimed = meizu_smartshrink_anon(memcg,
					min(32UL, target - done), lruvec, active);
			if (!reclaimed)
				break;
			cond_resched();
		}
	}
	return nbytes;
}

static int memcg_total_info_per_app_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = NULL;
	unsigned long anon_size;
	unsigned long zram_compress_size;
	unsigned long eswap_compress_size;
	unsigned long zram_page_size;
	unsigned long eswap_page_size;

	seq_printf(m, "%-8s %-8s %-8s %-8s %-8s %s \n",
			"anon", "zram_c", "zram_p", "eswap_c", "eswap_p",
			"memcg_n");
	while ((memcg = get_next_memcg(memcg))) {
		if (!MEMCGRP_ITEM_DATA(memcg))
			continue;

		anon_size = memcg_anon_pages(memcg);
		zram_compress_size = smartswap_read_memcg_stats(memcg,
				MCG_ZRAM_STORED_SZ);
		eswap_compress_size = smartswap_read_memcg_stats(memcg,
				MCG_DISK_STORED_SZ);
		zram_page_size = smartswap_read_memcg_stats(memcg,
				MCG_ZRAM_STORED_PG_SZ);
		eswap_page_size = smartswap_read_memcg_stats(memcg,
				MCG_DISK_STORED_PG_SZ);

		anon_size *= PAGE_SIZE / SZ_1K;
		zram_compress_size /= SZ_1K;
		eswap_compress_size /= SZ_1K;
		zram_page_size *= PAGE_SIZE / SZ_1K;
		eswap_page_size *= PAGE_SIZE / SZ_1K;

		seq_printf(m, "%-8lu %-8lu %-8lu %-8lu %-8lu %s \n",
				anon_size, zram_compress_size, zram_page_size,
				eswap_compress_size, eswap_page_size,
				MEMCGRP_ITEM(memcg, name));
	}

	return 0;
}

static int memcg_swap_stat_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = NULL;
	unsigned long eswap_out_cnt;
	unsigned long eswap_out_size;
	unsigned long eswap_in_size;
	unsigned long eswap_in_cnt;
	unsigned long page_fault_cnt;
	unsigned long cur_eswap_size;
	unsigned long max_eswap_size;
	unsigned long zram_compress_size, zram_page_size;
	unsigned long eswap_compress_size, eswap_page_size;

	memcg = mem_cgroup_from_css(seq_css(m));

	zram_compress_size = smartswap_read_memcg_stats(memcg, MCG_ZRAM_STORED_SZ);
	zram_page_size = smartswap_read_memcg_stats(memcg, MCG_ZRAM_STORED_PG_SZ);
	eswap_compress_size = smartswap_read_memcg_stats(memcg, MCG_DISK_STORED_SZ);
	eswap_page_size = smartswap_read_memcg_stats(memcg, MCG_DISK_STORED_PG_SZ);

	eswap_out_cnt = smartswap_read_memcg_stats(memcg, MCG_ESWAPOUT_CNT);
	eswap_out_size = smartswap_read_memcg_stats(memcg, MCG_ESWAPOUT_SZ);
	eswap_in_size = smartswap_read_memcg_stats(memcg, MCG_ESWAPIN_SZ);
	eswap_in_cnt = smartswap_read_memcg_stats(memcg, MCG_ESWAPIN_CNT);
	page_fault_cnt = smartswap_read_memcg_stats(memcg, MCG_DISK_FAULT_CNT);
	cur_eswap_size = smartswap_read_memcg_stats(memcg, MCG_DISK_SPACE);
	max_eswap_size = smartswap_read_memcg_stats(memcg, MCG_DISK_SPACE_PEAK);

	seq_printf(m, "%-32s %12lu KB\n", "zramCompressedSize:",
			zram_compress_size / SZ_1K);
	seq_printf(m, "%-32s %12lu KB\n", "zramOrignalSize:",
			zram_page_size << (PAGE_SHIFT - 10));
	seq_printf(m, "%-32s %12lu KB\n", "eswapCompressedSize:",
			eswap_compress_size / SZ_1K);
	seq_printf(m, "%-32s %12lu KB\n", "eswapOrignalSize:",
			eswap_page_size << (PAGE_SHIFT - 10));
	seq_printf(m, "%-32s %12lu \n", "eswapOutTotal:", eswap_out_cnt);
	seq_printf(m, "%-32s %12lu KB\n", "eswapOutSize:", eswap_out_size / SZ_1K);
	seq_printf(m, "%-32s %12lu\n", "eswapInTotal:", eswap_in_cnt);
	seq_printf(m, "%-32s %12lu KB\n", "eswapInSize:", eswap_in_size / SZ_1K);
	seq_printf(m, "%-32s %12lu\n", "pageInTotal:", page_fault_cnt);
	seq_printf(m, "%-32s %12lu KB\n", "eswapSizeCur:", cur_eswap_size / SZ_1K);
	seq_printf(m, "%-32s %12lu KB\n", "eswapSizeMax:", max_eswap_size / SZ_1K);

	return 0;
}

static ssize_t mem_cgroup_name_write(struct kernfs_open_file *of, char *buf,
		size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	memcg_hybs_t *hybp = MEMCGRP_ITEM_DATA(memcg);
	int len, w_len;

	if (!hybp)
		return -EINVAL;

	buf = strstrip(buf);
	len = strlen(buf) + 1;
	if (len > MEM_CGROUP_NAME_MAX_LEN)
		len = MEM_CGROUP_NAME_MAX_LEN;

	w_len = snprintf(hybp->name, len, "%s", buf);
	if (w_len > len)
		hybp->name[len - 1] = '\0';

	return nbytes;
}

static int mem_cgroup_name_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	seq_printf(m, "%s\n", MEMCGRP_ITEM(memcg, name));

	return 0;
}

static int mem_cgroup_app_score_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg;
	memcg_hybs_t *hybs;

	if (val > MAX_APP_SCORE || val < 0)
		return -EINVAL;

	memcg = mem_cgroup_from_css(css);
	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs) {
		hybs = smartswap_cache_alloc(memcg, false);
		if (!hybs)
			return -EINVAL;
		/* Existing memcgs passed their online hook before this cache existed. */
		meizu_cache_pin_css(hybs, &memcg->css, true);
	}

	if (atomic64_read(&MEMCGRP_ITEM(memcg, app_score)) != val)
		atomic64_set(&MEMCGRP_ITEM(memcg, app_score), val);
	memcg_app_score_update(memcg);

	return 0;
}

static s64 mem_cgroup_app_score_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	return atomic64_read(&MEMCGRP_ITEM(memcg, app_score));
}

int mem_cgroup_app_uid_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg;
	memcg_hybs_t *hybs;

	if (val < 0)
		return -EINVAL;

	memcg = mem_cgroup_from_css(css);
	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs) {
		return -EINVAL;
	}

	if (atomic64_read(&MEMCGRP_ITEM(memcg, app_uid)) != val)
		atomic64_set(&MEMCGRP_ITEM(memcg, app_uid), val);

	return 0;
}

static s64 mem_cgroup_app_uid_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	return atomic64_read(&MEMCGRP_ITEM(memcg, app_uid));
}

static int mem_cgroup_ub_ufs2zram_ratio_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	if (val > MAX_RATIO || val < MIN_RATIO)
		return -EINVAL;

	atomic64_set(&MEMCGRP_ITEM(memcg, ub_ufs2zram_ratio), val);

	return 0;
}

static s64 mem_cgroup_ub_ufs2zram_ratio_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	return atomic64_read(&MEMCGRP_ITEM(memcg, ub_ufs2zram_ratio));
}

static int mem_cgroup_force_swapin_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	memcg_hybs_t *hybs;
	unsigned long size = 0;
	const unsigned int ratio = 100;

	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs)
		return -EPERM;

#ifdef	CONFIG_SMARTSWAP_CORE
	size = atomic64_read(&hybs->smartswap_stored_size);
#endif
	size = atomic64_read(&hybs->ub_ufs2zram_ratio) * size / ratio;
	size = EXTENT_ALIGN_UP(size);

#ifdef CONFIG_SMARTSWAP_CORE
	smartswap_batch_out(memcg, size, val ? true : false);
#endif

	return 0;
}

static int mem_cgroup_force_swapout_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
#ifdef CONFIG_SMARTSWAP_CORE
	smartswap_force_reclaim(mem_cgroup_from_css(css));
#endif
	return 0;
}

struct mem_cgroup *get_next_memcg(struct mem_cgroup *prev)
{
	struct mem_cgroup *cursor = prev;
	struct mem_cgroup *cursor_css_only = NULL;
	struct mem_cgroup *result = NULL;
	unsigned long flags;

	for (;;) {
		struct list_head *pos;
		struct mem_cgroup *failed_id = NULL;
		memcg_hybs_t *hybs;

		spin_lock_irqsave(&score_list_lock, flags);
		if (cursor) {
			hybs = MEMCGRP_ITEM_DATA(cursor);
			pos = hybs ? &hybs->score_node : NULL;
		} else {
			pos = &score_head;
		}
		/* Offline removes and initializes score_node; terminate that walk. */
		if (pos && !list_empty(pos)) {
			for (pos = pos->next; pos != &score_head; pos = pos->next) {
				struct mem_cgroup *memcg;

				hybs = list_entry(pos, memcg_hybs_t, score_node);
				memcg = hybs->memcg;
				if (!memcg || !css_tryget(&memcg->css))
					continue;
				/* A CSS reference alone does not prevent ID recycling. */
				if (refcount_inc_not_zero(&memcg->id.ref))
					result = memcg;
				else
					failed_id = memcg;
				break;
			}
		}
		spin_unlock_irqrestore(&score_list_lock, flags);

		/* Neither put can run under score_list_lock: final ID removal
		 * invokes the memcg hook, and final CSS release can free its cache.
		 */
		if (cursor_css_only) {
			css_put(&cursor_css_only->css);
			cursor_css_only = NULL;
		}
		if (result || !failed_id)
			break;
		/* Retain this failed candidate's CSS while using it as the next
		 * cursor, then release it after the next locked selection.
		 */
		cursor = failed_id;
		cursor_css_only = failed_id;
	}
	if (prev) {
		mem_cgroup_id_put2(prev);
		css_put(&prev->css);
	}
	return result;
}

void get_next_memcg_break(struct mem_cgroup *memcg)
{
	if (memcg) {
		mem_cgroup_id_put2(memcg);
		css_put(&memcg->css);
	}
}

#include "meizu-interface.inc"

static struct cftype mem_cgroup_smartswap_legacy_files[] = {
	{
		.name = "force_shrink_anon",
		.write = mem_cgroup_force_shrink_anon,
	},
	{
		.name = "total_info_per_app",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = memcg_total_info_per_app_show,
	},
	{
		.name = "swap_stat",
		.seq_show = memcg_swap_stat_show,
	},
	{
		.name = "name",
		.write = mem_cgroup_name_write,
		.seq_show = mem_cgroup_name_show,
	},
	{ .name = "id", .seq_show = meizu_memcg_id_show },
#ifdef CONFIG_SMARTSWAP_SWAPD
	{ .name = "watermark_ok", .seq_show = meizu_memcg_watermark_ok_show },
#endif
	{
		.name = "app_score",
		.write_s64 = mem_cgroup_app_score_write,
		.read_s64 = mem_cgroup_app_score_read,
	},
	{
		.name = "app_uid",
		.write_s64 = mem_cgroup_app_uid_write,
		.read_s64 = mem_cgroup_app_uid_read,
	},
	{
		.name = "ub_ufs2zram_ratio",
		.write_s64 = mem_cgroup_ub_ufs2zram_ratio_write,
		.read_s64 = mem_cgroup_ub_ufs2zram_ratio_read,
	},
	{
		.name = "force_swapin",
		.write_s64 = mem_cgroup_force_swapin_write,
	},
	{
		.name = "force_swapout",
		.write_s64 = mem_cgroup_force_swapout_write,
	},
#ifdef CONFIG_SMARTSWAP_CORE
	{
		.name = "psi",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = smartswap_psi_show,
	},
	{ .name = "lat_loglevel", .flags = CFTYPE_ONLY_ON_ROOT,
	  .write_s64 = mem_cgroup_lat_loglevel_write },
	{ .name = "lat", .flags = CFTYPE_ONLY_ON_ROOT,
	  .seq_show = meizu_smartswap_lat_show },
	{
		.name = "stored_wm_ratio",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.write_s64 = mem_cgroup_stored_wm_ratio_write,
		.read_s64 = mem_cgroup_stored_wm_ratio_read,
	},
#endif
	{ }, /* terminate */
};

static int smartswap_enable(struct zram *zram)
{
	int ret = 0;

	if (!smartswap_accepts_zram(zram))
		return -EBUSY;

	if (smartswap_enabled) {
		log_warn("enabled is true\n");
		return ret;
	}

#ifdef CONFIG_SMARTSWAP_SWAPD
	ret = swapd_init(zram);
	if (ret)
		return ret;
#endif

#ifdef CONFIG_SMARTSWAP_CORE
	ret = smartswap_core_enable();
	if (ret)
		goto smartswap_core_enable_fail;
#endif
	smartswap_enabled = true;

	return 0;

#ifdef CONFIG_SMARTSWAP_CORE
smartswap_core_enable_fail:
#endif
#ifdef CONFIG_SMARTSWAP_SWAPD
	swapd_exit();
#endif
	return ret;
}

static void smartswap_disable(struct zram * zram)
{
	if (!smartswap_enabled) {
		log_warn("enabled is false\n");
		return;
	}

#ifdef CONFIG_SMARTSWAP_CORE
	smartswap_core_disable();
#endif

#ifdef CONFIG_SMARTSWAP_SWAPD
	swapd_exit();
#endif
	smartswap_enabled = false;
}

/* Caller has drained core users and torn down the old zram table. */
void smartswap_cache_detach_zram(struct zram *zram)
{
	struct meizu_memcg_cache_entry *entry;
	unsigned long flags;

	spin_lock_irqsave(&meizu_cache_lock, flags);
	list_for_each_entry(entry, &meizu_cache_entries, node) {
		memcg_hybs_t *hybs = &entry->hybs;

		if (hybs->zram != zram)
			continue;
		WRITE_ONCE(hybs->zram, NULL);
		hybs->in_swapin = false;
		hybs->force_swapout = false;
		hybs->can_eswaped = 0;
		atomic64_set(&hybs->zram_stored_size, 0);
		atomic64_set(&hybs->zram_page_size, 0);
		atomic64_set(&hybs->smartswap_stored_pages, 0);
		atomic64_set(&hybs->smartswap_stored_size, 0);
		atomic_set(&hybs->smartswap_extcnt, 0);
		atomic_set(&hybs->smartswap_peakextcnt, 0);
	}
	spin_unlock_irqrestore(&meizu_cache_lock, flags);
}

void smartswap_reset_swapd(struct zram *zram)
{
	mutex_lock(&smartswap_enable_lock);
#ifdef CONFIG_SMARTSWAP_SWAPD
	if (smartswap_enabled)
		swapd_exit();
#endif
	smartswap_enabled = false;
	mutex_unlock(&smartswap_enable_lock);
}

ssize_t smartswap_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len = snprintf(buf, PAGE_SIZE, "smartswap %s reclaim_in %s swapd %s\n",
			smartswap_core_enabled() ? "enable" : "disable",
			smartswap_reclaim_in_enable() ? "enable" : "disable",
			smartswap_swapd_enabled() ? "enable" : "disable");

	return len;
}

ssize_t smartswap_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned long val;
	char *kbuf;
	struct zram *zram;

	kbuf = strstrip((char *)buf);
	ret = kstrtoul(kbuf, 0, &val);
	if (unlikely(ret)) {
		log_err("val %s is invalid!\n", kbuf);

		return -EINVAL;
	}

	mutex_lock(&smartswap_enable_lock);
	zram = dev_to_zram(dev);
	if (val == 0)
		smartswap_disable(zram);
	else
		ret = smartswap_enable(zram);
	mutex_unlock(&smartswap_enable_lock);

	if (ret == 0)
		ret = len;
	return ret;
}

/* Alloc/online/offline/core producers must be stopped before this routine.
 * The final-free hook remains registered: it serializes real memcg destruction
 * with detachment under meizu_cache_lock, including already-offline memcgs.
 */
static void meizu_release_cache_registry(void)
{
	for (;;) {
		struct meizu_memcg_cache_entry *entry;
		struct mem_cgroup *memcg;
		unsigned long flags, score_flags;
		bool css_ref;

		spin_lock_irqsave(&meizu_cache_lock, flags);
		if (list_empty(&meizu_cache_entries)) {
			spin_unlock_irqrestore(&meizu_cache_lock, flags);
			break;
		}
		entry = list_first_entry(&meizu_cache_entries,
				struct meizu_memcg_cache_entry, node);
		memcg = entry->hybs.memcg;
		/* The free observer cannot return and free memcg under this lock. */
		cmpxchg(&memcg->android_oem_data1[0], (u64)&entry->hybs, 0);
		list_move_tail(&entry->node, &meizu_detached_cache_entries);
		css_ref = entry->online_css_ref;
		entry->online_css_ref = false;
		spin_lock_irqsave(&score_list_lock, score_flags);
		list_del_init(&entry->hybs.score_node);
		spin_unlock_irqrestore(&score_list_lock, score_flags);
		spin_unlock_irqrestore(&meizu_cache_lock, flags);

		if (css_ref)
			css_put(&memcg->css);
		if (refcount_read(&entry->hybs.usage) != 1)
			log_err("memcg cache still referenced at teardown: %u\n",
				refcount_read(&entry->hybs.usage));
		put_memcg_cache(&entry->hybs);
	}
	if (meizu_free_hook_registered) {
		UNREGISTER_HOOK(mem_cgroup_free);
		meizu_free_hook_registered = false;
	}
	tracepoint_synchronize_unregister();
	/* Internal owners must return before their final-put code can unload. */
	if (!wait_event_timeout(meizu_cache_wait, !atomic_read(&meizu_cache_count),
			msecs_to_jiffies(5000))) {
		struct meizu_memcg_cache_entry *entry;
		unsigned long flags;

		spin_lock_irqsave(&meizu_cache_lock, flags);
		list_for_each_entry(entry, &meizu_detached_cache_entries, node)
			log_err("residual cache source=%s memcg=%p name=%.*s usage=%u\n",
				entry->memcg_released ? "memcg_free" : "module_exit",
				entry->hybs.memcg, MEM_CGROUP_NAME_MAX_LEN,
				entry->hybs.name, refcount_read(&entry->hybs.usage));
		spin_unlock_irqrestore(&meizu_cache_lock, flags);
		wait_event(meizu_cache_wait, !atomic_read(&meizu_cache_count));
	}
	kmem_cache_destroy(smartswap_cache);
	smartswap_cache = NULL;
}

int __init smartswap_pre_init(void)
{
	int ret;

	INIT_LIST_HEAD(&score_head);
	log_level = HS_LOG_INFO;
	smartswap_cache = kmem_cache_create("mem_cgroup_smartswap",
			sizeof(struct meizu_memcg_cache_entry), 0, 0, NULL);
	if (!smartswap_cache)
		return -ENOMEM;
#ifdef CONFIG_SMARTSWAP_SWAPD
	swapd_pre_init();
#endif
	/* Observe memcg lifetime before publishing allocating cgroup controls. */
	ret = register_all_hooks();
	if (ret)
		goto fail_hooks;
	ret = cgroup_add_legacy_cftypes(&memory_cgrp_subsys,
			mem_cgroup_smartswap_legacy_files);
	if (ret)
		goto fail_main_cftypes;
#ifdef CONFIG_SMARTSWAP_SWAPD
	ret = cgroup_add_legacy_cftypes(&memory_cgrp_subsys,
			mem_cgroup_swapd_legacy_files);
	if (ret)
		goto fail_swapd_cftypes;
#endif
	log_info("smartswap inited success!\n");
	return 0;
#ifdef CONFIG_SMARTSWAP_SWAPD
fail_swapd_cftypes:
	cgroup_rm_cftypes(mem_cgroup_smartswap_legacy_files);
#endif
fail_main_cftypes:
	unregister_all_hook();
fail_hooks:
	tracepoint_synchronize_unregister();
#ifdef CONFIG_SMARTSWAP_SWAPD
	swapd_pre_deinit();
#endif
	meizu_release_cache_registry();
	return ret;
}

/* Shared by failed initialization and normal module removal. */
void smartswap_exit(void)
{
#ifdef CONFIG_SMARTSWAP_SWAPD
	cgroup_rm_cftypes(mem_cgroup_swapd_legacy_files);
#endif
	cgroup_rm_cftypes(mem_cgroup_smartswap_legacy_files);
	unregister_all_hook();
	tracepoint_synchronize_unregister();
#ifdef CONFIG_SMARTSWAP_SWAPD
	swapd_pre_deinit();
#endif
	meizu_release_cache_registry();
}
