/* SPDX-License-Identifier: GPL-2.0-only */
/* M2391 interface recovered from the original smartswap_zram module. */
unsigned int smartswap_fault_out_first;
unsigned int smartswap_shrink_af_ratio;
static u64 lat_loglevel;

ssize_t smartswap_fault_out_first_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "smartswap_fault_out_first %s\n",
			READ_ONCE(smartswap_fault_out_first) ? "yes" : "no");
}

static ssize_t meizu_smartswap_flag_store(unsigned int *flag,
		const char *buf, size_t count)
{
	char *text;
	u64 value;

	text = kmemdup_nul(buf, count, GFP_KERNEL);
	if (!text)
		return -ENOMEM;
	/* Preserve base-auto parsing, surrounding whitespace and low-u32 storage. */
	if (!kstrtoull(strim(text), 0, &value))
		WRITE_ONCE(*flag, (unsigned int)value);
	kfree(text);
	return count;
}

ssize_t smartswap_fault_out_first_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return meizu_smartswap_flag_store(&smartswap_fault_out_first, buf, count);
}

ssize_t smartswap_shrink_af_ratio_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "shrink_af_ration anon:file %d\n", READ_ONCE(smartswap_shrink_af_ratio));
}

ssize_t smartswap_shrink_af_ratio_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return meizu_smartswap_flag_store(&smartswap_shrink_af_ratio, buf, count);
}

static int mem_cgroup_lat_loglevel_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 value)
{
	WRITE_ONCE(lat_loglevel, (u64)value);
	return 0;
}

static const char *const meizu_cpuset_desc[] = {
	"BG", "FG", "TOP", "UX", "OTHERS",
};

void smartswap_record_fault_latency(s64 ns)
{
	struct smartswap_stat *stat = smartswap_get_stat_obj();
	struct cgroup_subsys_state *css;
	char name[33] = { 0 };
	const char *cpuset_name = name;
	s64 us;
	unsigned int group = 4, bucket;

	/* Original statistics are opt-in below the strict >100000 us slow-fault threshold. */
	if (!stat || (!READ_ONCE(lat_loglevel) && ns < 100001000))
		return;
	if (test_task_vip(current)) {
		group = 3;
	} else {
		rcu_read_lock();
		css = task_css(current, cpuset_cgrp_id);
		if (css && css_tryget(css)) {
			struct cgroup *parent = cgroup_parent(css->cgroup);

			rcu_read_unlock();
			/* The pinned cgroup holds its immutable parent.  This
			 * exported API copies /name under the kernfs rename lock;
			 * kernfs_name itself is not exported to modules.
			 */
			kernfs_path_from_node(css->cgroup->kn,
				parent ? parent->kn : NULL, name, sizeof(name));
			if (name[0] == '/')
				cpuset_name = name + 1;
			css_put(css);
		} else {
			rcu_read_unlock();
		}
		if (!strncmp(cpuset_name, "top-app", 7))
			group = 2;
		else if (!strncmp(cpuset_name, "foreground", 10))
			group = 1;
		else if (!strncmp(cpuset_name, "background", 10))
			group = 0;
	}
	us = div_s64(ns, 1000);
	/* Stock's first boundary is 1001 us; subsequent buckets use ceil(us/10ms). */
	if (ns < 1001000)
		bucket = 0;
	else if (us > 100000)
		bucket = 11;
	else
		bucket = ((u32)us - 1) / 10000 + 1;
	atomic64_inc(&stat->fault_lat_stat[group].total_cnt);
	atomic64_inc(&stat->fault_lat_stat[group].hist[bucket]);
}

static int meizu_smartswap_lat_show(struct seq_file *m, void *v)
{
	struct smartswap_stat *stat = smartswap_get_stat_obj();
	unsigned int group, bucket;

	if (!smartswap_core_enabled() || !stat)
		return -EINVAL;
	seq_printf(m, "fault_out_eswap: %lld\n", atomic64_read(&stat->smartswap_fault_cnt));
	seq_printf(m, "fault_out_share_eswap: %lld\n", atomic64_read(&stat->smartswap_fault_share_cnt));
	seq_printf(m, "fault_out_same_eswap: %lld\n", atomic64_read(&stat->smartswap_fault_same_cnt));
	for (group = 0; group < ARRAY_SIZE(meizu_cpuset_desc); group++) {
		seq_printf(m, "%s_latency_tot: %lld\n", meizu_cpuset_desc[group],
				atomic64_read(&stat->fault_lat_stat[group].total_cnt));
		for (bucket = 0; bucket < 12; bucket++) {
			s64 count = atomic64_read(&stat->fault_lat_stat[group].hist[bucket]);

			if (!bucket)
				seq_printf(m, " <=  1ms latency: %lld\n", count);
			else if (bucket == 11)
				seq_printf(m, " > 100ms latency: %lld\n", count);
			else
				seq_printf(m, " <=%3dms latency: %lld\n", bucket * 10, count);
		}
	}
	{
		static const char *const scene_names[] = {
			"reclaim_in", "fault_out", "batch_out", "pre_out",
		};
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(scene_names); i++) {
			seq_printf(m, "%s_latency_tot: %lld\n", scene_names[i],
					atomic64_read(&stat->lat[i].latency_tot));
			seq_printf(m, "%s_latency_max: %lld\n", scene_names[i],
					atomic64_read(&stat->lat[i].latency_max));
			seq_printf(m, "%s_timeout_cnt: %lld\n", scene_names[i],
					atomic64_read(&stat->lat[i].timeout_cnt));
		}
		for (i = 0; i < 2; i++) {
			seq_printf(m, "fault_out_timeout_100ms_cnt(%s): %lld\n", i ? "FG" : "BG",
					atomic64_read(&stat->fault_stat[i].timeout_100ms_cnt));
			seq_printf(m, "fault_out_timeout_500ms_cnt(%s): %lld\n", i ? "FG" : "BG",
					atomic64_read(&stat->fault_stat[i].timeout_500ms_cnt));
		}
	}
	return 0;
}

static int meizu_memcg_id_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));

	seq_printf(m, "id is %d\n", memcg->id.id);
	/* These two lines were fixed diagnostic constants in the original. */
	seq_puts(m, "offset- 400\noffset is  400\n");
	seq_printf(m, "sizeof(cgroup) %zu sizeof(mem_cgroup) %zu\n",
			sizeof(struct cgroup), sizeof(struct mem_cgroup));
	seq_printf(m, "sizeof(kernfs_node) %zu\n", sizeof(struct kernfs_node));
	/* Original diagnostics multiply per-CPU structures by the eight SoC CPUs,
	 * independently of CONFIG_NR_CPUS (32 in the original image).
	 */
	seq_printf(m, "sizeof(psi_group) %zu sizeof(psi_group_cpu) %zu   %zu\n",
			sizeof(struct psi_group), sizeof(struct psi_group_cpu),
			sizeof(struct psi_group_cpu) * 8);
	seq_printf(m, "sizeof(mem_cgroup_smartswap) %zu\n", sizeof(memcg_hybs_t));
	seq_printf(m, "sizeof(mem_cgroup_per_node) %zu\n", sizeof(struct mem_cgroup_per_node));
	seq_printf(m, "sizeof(lruvec_stats_percpu) %zu %zu\n",
			sizeof(struct lruvec_stats_percpu), sizeof(struct lruvec_stats_percpu) * 8);
	return 0;
}

#ifdef CONFIG_SMARTSWAP_SWAPD
extern unsigned int system_cur_avail_buffers(void);
extern unsigned int get_high_avail_buffers_value(void);
extern u64 get_compress_ratio_value(void);
extern u64 get_zram_wm_ratio_value(void);
extern u64 get_smartswap_meminfo(const char *type);

static int meizu_memcg_watermark_ok_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	memcg_hybs_t *hybs = MEMCGRP_ITEM_DATA(memcg);
	struct smartswap_stat *stat = smartswap_get_stat_obj();
	unsigned long used, total;
	u64 ratio, wm, new_size, stored, compressed;
	u32 high = get_high_avail_buffers_value();
	u32 avail = system_cur_avail_buffers();
	unsigned long flags;
	struct list_head *pos;

	if (!stat || !hybs)
		return -EINVAL;
	/* swapd owner publication/removal uses this same lock. */
	mutex_lock(&smartswap_enable_lock);
	used = get_smartswap_meminfo("pages_stored");
	total = get_nr_zram_total();
	ratio = used * 100UL / total;
	/* Preserve original unsigned subtraction/multiplication policy semantics. */
	wm = get_zram_wm_ratio_value() -
		((u64)(u32)(high - avail) * (get_compress_ratio_value() / 10) *
		 (SZ_1M / PAGE_SIZE) * 100 / total);
	wm = min(wm, get_zram_wm_ratio_value());
	seq_printf(m, "high_avail_buffers %d M\n", high);
	seq_printf(m, "system_cur_avail_buffers %d M\n", avail);
	seq_printf(m, "dff_buffes %d M\n", high - avail);
	seq_printf(m, "zram_used %ld cur_ratio %ld\n", used, (unsigned long)ratio);
	seq_printf(m, "zram_total %ld\n", total);
	/* Original printed this function's address as an int; expose its real value. */
	seq_printf(m, "zram_wm_ratio %d\n", (int)get_zram_wm_ratio_value());
	seq_printf(m, "wm %ld\n", (unsigned long)wm);
	seq_printf(m, "%s\n", ratio > wm ? "watermark_ok" : "watermark_not_ok");
	seq_printf(m, "@zram zram_stored_pages %ld @eswap stored_pages %ld smartswap_ratio_ok if @zram > @eswap\n",
			(long)atomic64_read(&stat->zram_stored_pages),
			(long)atomic64_read(&stat->stored_pages));
	compressed = atomic64_read(&hybs->zram_stored_size);
	stored = atomic64_read(&hybs->smartswap_stored_size);
	new_size = (compressed + stored) * atomic_read(&hybs->ub_zram2ufs_ratio) / 100;
	hybs->can_eswaped = READ_ONCE(hybs->force_swapout) ? compressed :
			(new_size > stored ? new_size - stored : 0);
	seq_printf(m, "smartswap_permcg_sz %ld\n", (long)hybs->can_eswaped);
	seq_printf(m, "zram_stored_size(afer-comp) %ld\n", (unsigned long)compressed);
	seq_printf(m, "smartswap_stored_size(cur_size) %ld\n", (unsigned long)stored);
	seq_printf(m, "new_size %ld\n", (unsigned long)new_size);
	seq_printf(m, "ub_zram2ufs_ratio %d\n", atomic_read(&hybs->ub_zram2ufs_ratio));
	spin_lock_irqsave(&score_list_lock, flags);
	list_for_each(pos, &score_head) {
		memcg_hybs_t *entry = list_entry(pos, memcg_hybs_t, score_node);

		seq_printf(m, "memcg_iter_all %d name %s\n", entry->memcg->id.id, entry->name);
	}
	spin_unlock_irqrestore(&score_list_lock, flags);
	mutex_unlock(&smartswap_enable_lock);
	return 0;
}
#endif
