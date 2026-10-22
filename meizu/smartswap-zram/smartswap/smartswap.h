// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef SMARTSWAP_H
#define SMARTSWAP_H
extern int __init smartswap_pre_init(void);
extern ssize_t smartswap_vmstat_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t smartswap_loglevel_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t smartswap_loglevel_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t smartswap_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t smartswap_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf);
#ifdef CONFIG_SMARTSWAP_CORE
void smartswap_reset_quiesce(struct zram *zram);
void smartswap_reset_finish(struct zram *zram);
void smartswap_exit(void);
void smartswap_global_setting_deinit(void);
extern void smartswap_track(struct zram *zram, u32 index, struct mem_cgroup *memcg);
extern void smartswap_untrack(struct zram *zram, u32 index);
extern int smartswap_fault_out(struct zram *zram, u32 index);
extern bool smartswap_delete(struct zram *zram, u32 index);

extern ssize_t smartswap_report_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t smartswap_stat_snap_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t smartswap_meminfo_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t smartswap_core_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t smartswap_core_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t smartswap_loop_device_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t smartswap_loop_device_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t backing_dev_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t backing_dev_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t smartswap_dev_life_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t smartswap_dev_life_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t smartswap_quota_day_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t smartswap_quota_day_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern ssize_t smartswap_zram_increase_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t smartswap_zram_increase_show(struct device *dev,
		struct device_attribute *attr, char *buf);
extern void smartswap_unbind(struct zram *zram);
#endif

#ifdef CONFIG_SMARTSWAP_SWAPD
extern ssize_t smartswap_swapd_pause_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len);
extern ssize_t smartswap_swapd_pause_show(struct device *dev,
		struct device_attribute *attr, char *buf);
#endif
static inline bool current_is_swapd(void)
{
#ifdef CONFIG_SMARTSWAP_SWAPD
	return (strncmp(current->comm, "smartswapd:", sizeof("smartswapd:") - 1) == 0);
#else
	return false;
#endif
}
#endif /* SMARTSWAP_H */

