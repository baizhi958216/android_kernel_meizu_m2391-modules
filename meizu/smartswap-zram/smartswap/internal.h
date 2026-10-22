// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef SMARTSWAP_INTERNAL_H
#define SMARTSWAP_INTERNAL_H

#include <linux/sched.h>
#include <linux/zsmalloc.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/memcontrol.h>

bool smartswap_devices_ready(void);
bool smartswap_accepts_new_work(void);
bool smartswap_accepts_zram(struct zram *zram);
extern struct mutex smartswap_enable_lock;
struct zram *smartswap_owner_get(int *idx);
void smartswap_owner_put(int idx);
void smartswap_schedule_deinit(void);
void smartswap_reset_quiesce(struct zram *zram);
void smartswap_reset_finish(struct zram *zram);
void smartswap_cache_detach_zram(struct zram *zram);
void smartswap_global_setting_deinit(void);
void smartswap_record_fault_latency(s64 ns);
void smartswap_reset_swapd(struct zram *zram);
extern unsigned int smartswap_fault_out_first;
extern unsigned int smartswap_shrink_af_ratio;

#define EXTENT_SHIFT        15
#define EXTENT_SIZE         (1UL << EXTENT_SHIFT)
#define EXTENT_PG_CNT		(EXTENT_SIZE >> PAGE_SHIFT)
#define EXTENT_SECTOR_SIZE	(EXTENT_PG_CNT << 3)
#define EXTENT_MAX_OBJ_CNT  (30 * EXTENT_PG_CNT)
#define EXTENT_MASK (~(EXTENT_SIZE - 1))
#define EXTENT_ALIGN_UP(size)	((size + EXTENT_SIZE - 1) & EXTENT_MASK)

#define MAX_FAIL_RECORD_NUM 10
#define MEM_CGROUP_NAME_MAX_LEN 32
#define MAX_APP_SCORE 1000

#define SMARTSWAP_QUOTA_DAY		0x280000000	/* 10G bytes */
#define SMARTSWAP_CHECK_INTERVAL	86400		/* 24 hour */

#define MAX_RATIO 100
#define MIN_RATIO 0

enum {
	HS_LOG_ERR = 0,
	HS_LOG_WARN,
	HS_LOG_INFO,
	HS_LOG_DEBUG,
	HYB_MAX
};

void smartswap_loglevel_set(int level);
int smartswap_loglevel(void);

#define DUMP_STACK_ON_ERR 0
#define pt(l, f, ...)	pr_err(" <%d>[%s:%d] [%s:%d]: "f, l, \
			       current->comm, current->tgid, \
			       __func__, __LINE__, ##__VA_ARGS__)
static inline void pr_none(void) {}
#define hybp(l, f, ...) do {\
	(l <= smartswap_loglevel()) ? pt(l, f, ##__VA_ARGS__) : pr_none();\
	if (DUMP_STACK_ON_ERR && l == HS_LOG_ERR) dump_stack();\
} while (0)

#define log_err(f, ...)	hybp(HS_LOG_ERR, f, ##__VA_ARGS__)
#define log_warn(f, ...) hybp(HS_LOG_WARN, f, ##__VA_ARGS__)
#define log_info(f, ...) hybp(HS_LOG_INFO, f, ##__VA_ARGS__)
#define log_dbg(f, ...) hybp(HS_LOG_DEBUG, f, ##__VA_ARGS__)

enum smartswap_scene {
	SCENE_RECLAIM_IN = 0,
	SCENE_FAULT_OUT,
	SCENE_BATCH_OUT,
	SCENE_PRE_OUT,
	SCENE_MAX
};

enum smartswap_stage {
	STAGE_START = 0,
	STAGE_INIT,
	STAGE_IOENTRY_ALLOC,
	STAGE_FIND_EXTENT,
	STAGE_IO_EXTENT,
	STAGE_SEGMENT_ALLOC,
	STAGE_BIO_ALLOC,
	STAGE_SUBMIT_BIO,
	STAGE_END_IO,
	STAGE_SCHED_WORK,
	STAGE_END_WORK,
	STAGE_CALL_BACK,
	STAGE_WAKE_UP,
	STAGE_ZRAM_LOCK,
	STAGE_DONE,
	STAGE_MAX
};

enum smartswap_memcg_member {
	MCG_ZRAM_STORED_SZ = 0,
	MCG_ZRAM_STORED_PG_SZ,
	MCG_DISK_STORED_SZ,
	MCG_DISK_STORED_PG_SZ,
	MCG_ANON_FAULT_CNT,
	MCG_DISK_FAULT_CNT,
	MCG_ESWAPOUT_CNT,
	MCG_ESWAPOUT_SZ,
	MCG_ESWAPIN_CNT,
	MCG_ESWAPIN_SZ,
	MCG_DISK_SPACE,
	MCG_DISK_SPACE_PEAK,
};

enum smartswap_stage_err {
	SCENE_FAULT_OUT_INIT_FAIL = 0,
	SCENE_FAULT_OUT_ENTRY_ALLOC_FAIL,
	SCENE_FAULT_OUT_IO_ENTRY_PARA_FAIL,
	SCENE_FAULT_OUT_SEGMENT_ALLOC_FAIL,
	SCENE_FAULT_OUT_BIO_ALLOC_FAIL,
	SCENE_FAULT_OUT_BIO_ADD_FAIL,
	SCENE_FAULT_OUT_IO_FAIL,
	STAGE_ERR_MAX
};

struct smartswap;
struct fault_out_ctrl {
	bool init;
	int ext_id;
	int magic;
	struct smartswap *hs_swap;
	spinlock_t lock;
	atomic_t count;
	bool ext_faulting;
	struct task_struct *task_faulting;
	struct completion fault_out_finish_flag;
	struct delayed_work fault_out_defer_work;
	/* Private completion state; no exported type depends on this tail. */
	bool free_pending;
	bool finished;
	int result;
};
struct smartswap_fault_lat {
	atomic64_t total_cnt;
	atomic64_t hist[12];
};

struct smartswap {
	unsigned long size;
	int nr_objs;
	int nr_exts;
	int nr_mcgs;

	unsigned long *bitmap;
	atomic_t last_alloc_bit;

	struct hs_list_table *ext_table;
	struct hs_list_head *ext;

	struct hs_list_table *obj_table;
	struct hs_list_head *rmap;
	struct hs_list_head *lru;

	atomic_t stored_exts;
	atomic_t *ext_stored_pages;
	struct fault_out_ctrl *fault_out_ctrl;
	struct workqueue_struct *fault_out_queue;

	unsigned int mcg_id_cnt[MEM_CGROUP_ID_MAX + 1];
};

struct smartswap_record_err {
	unsigned char task_comm[TASK_COMM_LEN];
	enum smartswap_stage_err point;
	ktime_t time;
	u32 index;
	int ext_id;
};

struct smartswap_record_err_info {
	int num;
	spinlock_t lock;
	struct smartswap_record_err record[MAX_FAIL_RECORD_NUM];
};

struct smartswap_stage_info {
	unsigned int record_cnt;
	unsigned int end_cnt;
	ktime_t first_time;
	ktime_t last_time;
	s64 proc_total_time;
	s64 proc_max_time;
	unsigned long long last_ravg_sum;
	unsigned long long proc_ravg_sum;
	spinlock_t time_lock;
};

struct smartswap_record_stage {
	struct timer_list lat_monitor;
	unsigned long warning_threshold;
	int page_cnt;
	int segment_cnt;
	int nice;
	bool timeout_flag;
	unsigned char task_comm[TASK_COMM_LEN];
	struct task_struct *task;
	enum smartswap_scene scene;
	struct smartswap_stage_info key_point[STAGE_MAX];
};

struct smartswap_stat_latency {
	atomic64_t latency_tot;
	atomic64_t latency_max;
	atomic64_t timeout_cnt;
};

struct smartswap_fault_timeout_cnt{
	atomic64_t timeout_100ms_cnt;
	atomic64_t timeout_500ms_cnt;
};

struct smartswap_stat {
	atomic64_t reclaimin_cnt;
	atomic64_t reclaimin_bytes;
	atomic64_t reclaimin_real_load;
	atomic64_t reclaimin_bytes_daily;
	atomic64_t reclaimin_pages;
	atomic64_t reclaimin_infight;
	atomic64_t batchout_cnt;
	atomic64_t batchout_bytes;
	atomic64_t batchout_real_load;
	atomic64_t batchout_pages;
	atomic64_t batchout_inflight;
	atomic64_t fault_cnt;
	atomic64_t smartswap_fault_cnt;
	atomic64_t smartswap_fault_share_cnt;
	atomic64_t smartswap_fault_same_cnt;
	atomic64_t reout_pages;
	atomic64_t reout_bytes;
	atomic64_t zram_stored_pages;
	atomic64_t zram_stored_size;
	atomic64_t stored_pages;
	atomic64_t stored_size;
	atomic64_t notify_free;
	atomic64_t frag_cnt;
	atomic64_t mcg_cnt;
	atomic64_t ext_cnt;
	atomic64_t miss_free;
	atomic64_t mcgid_clear;
	atomic64_t skip_track_cnt;
	atomic64_t used_swap_pages;
	atomic64_t null_memcg_skip_track_cnt;
	atomic64_t stored_wm_ratio;
	atomic64_t dropped_ext_size;
	atomic64_t io_fail_cnt[SCENE_MAX];
	atomic64_t alloc_fail_cnt[SCENE_MAX];
	struct smartswap_stat_latency lat[SCENE_MAX];
	struct smartswap_fault_timeout_cnt fault_stat[2]; /* 0:bg 1:fg */
	struct smartswap_record_err_info record;
	struct smartswap_fault_lat fault_lat_stat[5];
};

struct smartswap_page_pool {
	struct list_head page_pool_list;
	spinlock_t page_pool_lock;
};

struct io_extent {
	int ext_id;
	struct zram *zram;
	struct mem_cgroup *mcg;
	struct page *pages[EXTENT_PG_CNT];
	u32 index[EXTENT_MAX_OBJ_CNT];
	int cnt;
	int real_load;

	struct smartswap_page_pool *pool;
};

struct smartswap_buffer {
	struct zram *zram;
	struct smartswap_page_pool *pool;
	struct page **dest_pages;
};

struct smartswap_entry {
	int ext_id;
	sector_t addr;
	struct page **dest_pages;
	int pages_sz;
	struct list_head list;
	void *private;
	void *manager_private;
};

struct smartswap_io_req;
struct smartswap_io {
	struct block_device *bdev;
	enum smartswap_scene scene;
	void (*done_callback)(struct smartswap_entry *, int, struct smartswap_io_req *);
	void (*complete_notify)(void *);
	void *private;
	struct smartswap_record_stage *record;
};

struct smartswap_io_req {
	struct smartswap_io io_para;
	struct kref refcount;
	struct mutex refmutex;
	struct wait_queue_head io_wait;
	atomic_t extent_inflight;
	struct completion io_end_flag;
	struct smartswap_segment *segment;
	bool limit_inflight_flag;
	bool wait_io_finish_flag;
	int page_cnt;
	int segment_cnt;
	int nice;
	atomic64_t real_load;
};

/* Change smartswap_event_item, you should change swapd_text togather*/
enum smartswap_event_item {
#ifdef CONFIG_SMARTSWAP_SWAPD
	SWAPD_WAKEUP,
	SWAPD_REFAULT,
	SWAPD_MEDIUM_PRESS,
	SWAPD_CRITICAL_PRESS,
	SWAPD_MEMCG_RATIO_SKIP,
	SWAPD_MEMCG_REFAULT_SKIP,
	SWAPD_SHRINK_ANON,
	SWAPD_SWAPOUT,
	SWAPD_SKIP_SWAPOUT,
	SWAPD_EMPTY_ROUND,
	SWAPD_OVER_MIN_BUFFER_SKIP_TIMES,
	SWAPD_EMPTY_ROUND_SKIP_TIMES,
	SWAPD_SNAPSHOT_TIMES,
	SWAPD_SKIP_SHRINK_OF_WINDOW,
	SWAPD_MANUAL_PAUSE,
#ifdef CONFIG_OPLUS_JANK
	SWAPD_CPU_BUSY_SKIP_TIMES,
	SWAPD_CPU_BUSY_BREAK_TIMES,
#endif
#endif
	NR_EVENT_ITEMS
};

struct swapd_event_state {
	unsigned long event[NR_EVENT_ITEMS];
};

typedef struct mem_cgroup_smartswap {
#ifdef CONFIG_SMARTSWAP
	atomic64_t ub_ufs2zram_ratio;
	atomic_t ub_zram2ufs_ratio;
	atomic64_t app_score;
	atomic64_t app_uid;
	struct list_head score_node;
	char name[MEM_CGROUP_NAME_MAX_LEN];
	struct zram *zram;
	struct mem_cgroup *memcg;
	refcount_t usage;
#endif
#ifdef CONFIG_SMARTSWAP_SWAPD
	atomic_t ub_mem2zram_ratio;
	atomic_t refault_threshold;
	unsigned long long reclaimed_pagefault;
	long long can_reclaimed;
#endif
#ifdef CONFIG_SMARTSWAP_CORE
	unsigned long zram_lru;
	unsigned long ext_lru;
	struct list_head link_list;
	spinlock_t zram_init_lock;
	long long can_eswaped;

	atomic64_t zram_stored_size;
	atomic64_t zram_page_size;
	unsigned long zram_watermark;

	atomic_t smartswap_extcnt;
	atomic_t smartswap_peakextcnt;

	atomic64_t smartswap_stored_pages;
	atomic64_t smartswap_stored_size;
	atomic64_t smartswap_ext_notify_free;

	atomic64_t smartswap_outcnt;
	atomic64_t smartswap_incnt;
	atomic64_t smartswap_allfaultcnt;
	atomic64_t smartswap_faultcnt;
	atomic64_t smartswap_faultcnt_share;
	atomic64_t smartswap_faultcnt_same;

	atomic64_t smartswap_outextcnt;
	atomic64_t smartswap_inextcnt;

	struct mutex swap_lock;
	bool in_swapin;
	bool force_swapout;
#endif
}memcg_hybs_t;

#define MEMCGRP_ITEM_DATA(memcg) ((memcg_hybs_t *)(memcg)->android_oem_data1[0])
#define MEMCGRP_ITEM(memcg, item) (MEMCGRP_ITEM_DATA(memcg)->item)

extern void __put_memcg_cache(memcg_hybs_t *hybs);

static inline memcg_hybs_t *get_memcg_cache(memcg_hybs_t *hybs)
{
	refcount_inc(&hybs->usage);
	return hybs;
}

static inline void put_memcg_cache(memcg_hybs_t *hybs)
{
	if (refcount_dec_and_test(&hybs->usage))
		__put_memcg_cache(hybs);
}

DECLARE_PER_CPU(struct swapd_event_state, swapd_event_states);
extern struct mutex reclaim_para_lock;

static inline void __count_swapd_event(enum smartswap_event_item item)
{
	raw_cpu_inc(swapd_event_states.event[item]);
}

static inline void count_swapd_event(enum smartswap_event_item item)
{
	this_cpu_inc(swapd_event_states.event[item]);
}

static inline void __count_swapd_events(enum smartswap_event_item item, long delta)
{
	raw_cpu_add(swapd_event_states.event[item], delta);
}

static inline void count_swapd_events(enum smartswap_event_item item, long delta)
{
	this_cpu_add(swapd_event_states.event[item], delta);
}

void *smartswap_malloc(size_t size, bool fast, bool nofail);
void smartswap_free(const void *mem);
unsigned long smartswap_zsmalloc(struct zs_pool *zs_pool,
		size_t size, struct smartswap_page_pool *pool);
struct page *smartswap_alloc_page(
		struct smartswap_page_pool *pool, gfp_t gfp,
		bool fast, bool nofail);
void smartswap_page_recycle(struct page *page,
		struct smartswap_page_pool *pool);
struct smartswap_stat *smartswap_get_stat_obj(void);
int smartswap_mgr_init(struct zram *zram);
void smartswap_memcg_init(struct zram *zram,
		struct mem_cgroup *memcg);
void smartswap_memcg_deinit(struct mem_cgroup *mcg);
void smartswap_zram_lru_add(struct zram *zram, u32 index,
		struct mem_cgroup *memcg);
void smartswap_zram_lru_del(struct zram *zram, u32 index);
unsigned long smartswap_extent_create(struct mem_cgroup *memcg,
		int *ext_id,
		struct smartswap_buffer *dest_buf,
		void **private);
void smartswap_extent_register(void *private, struct smartswap_io_req *req);
void smartswap_extent_objs_del(struct zram *zram, u32 index);
int smartswap_find_extent_by_idx(
		unsigned long eswpentry, struct smartswap_buffer *buf, void **private);
int smartswap_find_extent_by_memcg(
		struct mem_cgroup *mcg,
		struct smartswap_buffer *dest_buf, void **private);
void smartswap_extent_destroy(void *private, enum smartswap_scene scene);
void smartswap_extent_exception(enum smartswap_scene scene,
		void *private);
void smartswap_mgr_deinit(struct zram *zram);
struct mem_cgroup *smartswap_zram_get_memcg(struct zram *zram, u32 index);
int smartswap_schedule_init(void);
void *smartswap_plug_start(struct smartswap_io *io_para);
int smartswap_read_extent(void *io_handler,
		struct smartswap_entry *io_entry);
int smartswap_write_extent(void *io_handler,
		struct smartswap_entry *io_entry);

int smartswap_plug_finish(void *io_handler);

void perf_begin(
		struct smartswap_record_stage *record,
		ktime_t stsrt, unsigned long long start_ravg_sum,
		enum smartswap_scene scene);

void perf_end(struct smartswap_record_stage *record);

void perf_latency_begin(
		struct smartswap_record_stage *record,
		enum smartswap_stage type);

void perf_latency_end(
		struct smartswap_record_stage *record,
		enum smartswap_stage type);

void smartswap_perf_lat_point(
		struct smartswap_record_stage *record,
		enum smartswap_stage type);

void perf_async_set(
		struct smartswap_record_stage *record,
		enum smartswap_stage type, ktime_t start,
		unsigned long long start_ravg_sum);

void perf_stat_io(
		struct smartswap_record_stage *record, int page_cnt,
		int segment_cnt);

static inline unsigned long long smartswap_get_ravg_sum(void)
{
	return 0;
}

void smartswap_error_record(enum smartswap_stage_err point,
		u32 index, int ext_id, unsigned char *task_comm);
bool smartswap_reach_life_protect(void);
struct workqueue_struct *smartswap_get_reclaim_workqueue(void);
extern struct mem_cgroup *get_next_memcg(struct mem_cgroup *prev);
extern void get_next_memcg_break(struct mem_cgroup *prev);
extern memcg_hybs_t *smartswap_cache_alloc(struct mem_cgroup *memcg, bool atomic);
extern void memcg_app_score_resort(void);
extern unsigned long memcg_anon_pages(struct mem_cgroup *memcg);

#ifdef CONFIG_SMARTSWAP_CORE
extern bool smartswap_core_enabled(void);
extern bool smartswap_reclaim_in_pause(void);
extern void smartswap_set_reclaim_in_pause(void);
extern void smartswap_clear_reclaim_in_pause(void);
extern bool smartswap_reclaim_in_enable(void);
extern void smartswap_mem_cgroup_deinit(struct mem_cgroup *memcg);
extern unsigned long smartswap_reclaim_in(unsigned long size);
extern int smartswap_batch_out(struct mem_cgroup *mcg,
		unsigned long size, bool preload);
extern unsigned long zram_zsmalloc(struct zs_pool *zs_pool,
		size_t size, gfp_t gfp);
extern struct task_struct *get_task_from_proc(struct inode *inode);
extern unsigned long smartswap_read_zram_used_pages(void);
extern unsigned long long smartswap_read_zram_pagefault(void);
extern bool is_smartswap_reclaim_work_running(void);
extern void smartswap_force_reclaim(struct mem_cgroup *mcg);
extern bool smartswap_stored_wm_ok(void);
extern void mem_cgroup_id_remove_hook(void *data, struct mem_cgroup *memcg);
extern int mem_cgroup_stored_wm_ratio_write(
	struct cgroup_subsys_state *css, struct cftype *cft, s64 val);
extern s64 mem_cgroup_stored_wm_ratio_read(
	struct cgroup_subsys_state *css, struct cftype *cft);
extern bool smartswap_delete(struct zram *zram, u32 index);
extern int smartswap_stored_info(unsigned long *total, unsigned long *used);

extern unsigned long long smartswap_read_memcg_stats(
	struct mem_cgroup *mcg, enum smartswap_memcg_member mcg_member);
extern int smartswap_core_enable(void);
extern void smartswap_core_disable(void);
extern int smartswap_psi_show(struct seq_file *m, void *v);
#else
static inline unsigned long long smartswap_read_memcg_stats(
        struct mem_cgroup *mcg, enum smartswap_memcg_member mcg_member)
{
	return 0;
}

static inline unsigned long smartswap_read_zram_used_pages(void)
{
	return 0;
}

static inline unsigned long long smartswap_read_zram_pagefault(void)
{
	return 0;
}

static inline bool is_smartswap_reclaim_work_running(void)
{
	return false;
}

static inline bool smartswap_core_enabled(void) { return false; }
static inline bool smartswap_reclaim_in_enable(void) { return false; }
#endif

#ifdef CONFIG_SMARTSWAP_SWAPD
extern atomic_long_t fault_out_pause;
extern atomic_long_t fault_out_pause_cnt;
extern struct cftype mem_cgroup_swapd_legacy_files[];
extern bool zram_watermark_ok(void);
extern void wake_all_swapd(void);
extern void alloc_pages_slowpath_hook(void *data, gfp_t gfp_mask,
        unsigned int order, unsigned long delta);
extern void rmqueue_hook(void *data, struct zone *preferred_zone,
	struct zone *zone, unsigned int order, gfp_t gfp_flags,
	unsigned int alloc_flags, int migratetype);
extern void __init swapd_pre_init(void);
extern void swapd_pre_deinit(void);
extern void update_swapd_memcg_param(struct mem_cgroup *memcg);
extern bool free_zram_is_ok(void);
extern unsigned long get_nr_zram_total(void);
extern int swapd_init(struct zram *zram);
extern void swapd_exit(void);
extern bool smartswap_swapd_enabled(void);
#else
static inline bool smartswap_swapd_enabled(void) { return false; }
#endif

#endif /* end of SMARTSWAP_INTERNAL_H */

