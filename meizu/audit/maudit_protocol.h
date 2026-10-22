/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_MAUDIT_PROTOCOL_H
#define _MEIZU_MAUDIT_PROTOCOL_H
#include <linux/types.h>
/* Native ARM64 protocol, recovered from original BTF and ioctl dispatch. */
#define MAUDIT_SET_NAME 0x4108c801U
#define MAUDIT_REGISTER_PROBE 0x4120c802U
#define MAUDIT_UNREGISTER_PROBE 0x4120c803U
#define MAUDIT_VERSION 0xdeedbef0UL
#define MAUDIT_MAX_PROBES 1000
struct maudit_set_name {
	unsigned long version;
	char name[256];
};
struct maudit_kprobe_desc {
	unsigned long probeid;
	u16 mode;
	u16 logcount;
	u32 overtime_ms;
	u32 threshold;
	u32 uid_range[2];
	char name[256];
};
struct maudit_log_entry {
	u32 pid;
	u32 tid;
	u32 uid;
	unsigned long jiffies;
};
struct maudit_logger {
	unsigned long probeid;
	u32 logcount;
	struct maudit_log_entry entrys[1];
};
#endif
