/* M2391 coalesces simultaneous faults against the same backing extent.
 * Slot references prevent deletion until every fault returns. Extent refs
 * independently prevent backing-store reuse while completion waiters exist.
 */
static void smartswap_free_extent_now(struct smartswap *hs, int ext_id)
{
	hs_list_set_mcgid(ext_idx(hs, ext_id), hs->ext_table, 0);
	if (WARN_ON_ONCE(!test_and_clear_bit(extent_id2bit(hs, ext_id), hs->bitmap)))
		return;
	atomic_dec(&hs->stored_exts);
}

static void smartswap_free_extent_fault_out_defer_work(struct work_struct *work)
{
	struct fault_out_ctrl *ctrl = container_of(to_delayed_work(work),
		struct fault_out_ctrl, fault_out_defer_work);
	struct task_struct *task;
	unsigned long flags;

	spin_lock_irqsave(&ctrl->lock, flags);
	if (atomic_read(&ctrl->count)) {
		spin_unlock_irqrestore(&ctrl->lock, flags);
		queue_delayed_work(ctrl->hs_swap->fault_out_queue,
			&ctrl->fault_out_defer_work, 1);
		return;
	}
	task = ctrl->task_faulting;
	ctrl->task_faulting = NULL;
	ctrl->ext_faulting = false;
	ctrl->free_pending = false;
	/* Publish availability only after the old fault generation is closed. */
	smartswap_free_extent_now(ctrl->hs_swap, ctrl->ext_id);
	spin_unlock_irqrestore(&ctrl->lock, flags);
	if (task)
		put_task_struct(task);
}

static void smartswap_fault_ctrl_put(struct fault_out_ctrl *ctrl)
{
	struct task_struct *task = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ctrl->lock, flags);
	if (atomic_dec_and_test(&ctrl->count) && !ctrl->free_pending) {
		ctrl->ext_faulting = false;
		task = ctrl->task_faulting;
		ctrl->task_faulting = NULL;
	}
	spin_unlock_irqrestore(&ctrl->lock, flags);
	if (task)
		put_task_struct(task);
}

static void smartswap_fault_shared_stat(struct zram *zram, u32 index, bool same)
{
	struct smartswap_stat *stat = smartswap_get_stat_obj();
	struct mem_cgroup *memcg = smartswap_zram_get_memcg(zram, index);
	memcg_hybs_t *hybs = memcg ? MEMCGRP_ITEM_DATA(memcg) : NULL;

	if (same) {
		if (stat)
			atomic64_inc(&stat->smartswap_fault_same_cnt);
		if (hybs)
			atomic64_inc(&hybs->smartswap_faultcnt_same);
	} else {
		if (stat)
			atomic64_inc(&stat->smartswap_fault_share_cnt);
		if (hybs)
			atomic64_inc(&hybs->smartswap_faultcnt_share);
	}
}

static void smartswap_fault_slot_put(struct zram *zram, u32 index)
{
	if (atomic_dec_and_test(&zram->table[index].count))
		zram_clear_flag(zram, index, ZRAM_BATCHING_OUT);
}

/* Called with the slot locked; returns with it locked on every nonleader
 * path. A leader returns with the slot unlocked, as required by plug I/O.
 */
static int smartswap_fault_join(struct zram *zram, u32 index,
	unsigned long *zentry)
{
	struct fault_out_ctrl *ctrl;
	unsigned long flags;
	int ext_id, ret;
	bool leader;

	*zentry = zram_get_handle(zram, index);
	ext_id = (*zentry >> EXTENT_SHIFT);
	if (ext_id < 0 || ext_id >= zram->hs_swap->nr_exts)
		return -EIO;
	if (atomic_inc_return(&zram->table[index].count) == 1)
		zram_set_flag(zram, index, ZRAM_BATCHING_OUT);
	else
		smartswap_fault_shared_stat(zram, index, true);
	ctrl = &zram->hs_swap->fault_out_ctrl[ext_id];
	spin_lock_irqsave(&ctrl->lock, flags);
	leader = !ctrl->ext_faulting;
	if (leader) {
		ctrl->ext_faulting = true;
		ctrl->finished = false;
		ctrl->result = 0;
		ctrl->task_faulting = current;
		get_task_struct(current);
		reinit_completion(&ctrl->fault_out_finish_flag);
	}
	atomic_inc(&ctrl->count);
	spin_unlock_irqrestore(&ctrl->lock, flags);
	if (!leader)
		smartswap_fault_shared_stat(zram, index, false);
	zram_slot_unlock(zram, index);
	if (leader)
		return 1;
	ret = wait_for_completion_io_timeout(&ctrl->fault_out_finish_flag,
		msecs_to_jiffies(5000)) ? READ_ONCE(ctrl->result) : -ETIMEDOUT;
	zram_slot_lock(zram, index);
	if (!ret && zram_test_flag(zram, index, ZRAM_WB))
		ret = -EIO;
	smartswap_fault_slot_put(zram, index);
	smartswap_fault_ctrl_put(ctrl);
	return ret;
}

static void smartswap_fault_complete(struct zram *zram,
	unsigned long zentry, int result)
{
	struct fault_out_ctrl *ctrl =
		&zram->hs_swap->fault_out_ctrl[(zentry >> EXTENT_SHIFT)];
	unsigned long flags;

	spin_lock_irqsave(&ctrl->lock, flags);
	ctrl->result = result;
	ctrl->finished = true;
	complete_all(&ctrl->fault_out_finish_flag);
	spin_unlock_irqrestore(&ctrl->lock, flags);
	smartswap_fault_ctrl_put(ctrl);
}
