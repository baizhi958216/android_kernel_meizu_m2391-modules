/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_STORAGE_RECOVERED_H
#define _MEIZU_STORAGE_RECOVERED_H

#include <linux/notifier.h>
#include <linux/types.h>

int private_slot_rw(int rw, int slot, void *buf, size_t len);
int private_slot_rw_speaker(int rw, void *buf, size_t len);
int param_slot_rw(int rw, int slot, void *buf, size_t len);
int sd_partition_rw(const char *part_name, int rw, loff_t offset,
		void *buf, size_t len);
int sd_is_storage_ready(const char *part_name);
int sd_get_storage_block_size(void);
int register_meizu_storage_notifier(struct notifier_block *nb);
#endif
