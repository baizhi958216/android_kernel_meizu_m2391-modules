// SPDX-License-Identifier: GPL-2.0-only
/* Original fifteen /proc/cs_press interfaces, recovered from proc_ops. */
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <asm/unaligned.h>

#include "cs_f71.h"

static int cs_f71_proc_show_locked(struct seq_file *seq,
		struct cs_f71_proc *entry)
{
	struct cs_f71 *cs = entry->cs;
	u8 bytes[64];
	u16 gears[4];
	unsigned int i;
	int ret, length;

	switch (entry->id) {
	case CS_F71_PROC_INFO:
		ret = cs_f71_rw_test_locked(cs, 0x67);
		if (!ret)
			ret = cs_f71_read(cs, 0x10, bytes, 6);
		if (ret)
			seq_puts(seq, "read fw info err\n");
		else
			seq_printf(seq, "fw_ver: %d %d %d %d\n", bytes[0],
					bytes[1], bytes[2], bytes[3]);
		return 0;
	case CS_F71_PROC_BOOT:
		ret = cs_f71_boot_info_locked(cs, bytes);
		if (ret)
			seq_puts(seq, "ERR\n");
		else
			seq_printf(seq, "%02X %02X %02X %02X\n", bytes[0],
					bytes[1], bytes[2], bytes[3]);
		return 0;
	case CS_F71_PROC_REG:
		if (!cs->register_cache_length) {
			seq_puts(seq, "rw reg failed\n");
			return 0;
		}
		for (i = 0; i < cs->register_cache_length; i++)
			seq_printf(seq, "%02x ", cs->register_cache[i]);
		seq_putc(seq, '\n');
		return 0;
	case CS_F71_PROC_RAW:
	case CS_F71_PROC_FORCE:
	case CS_F71_PROC_EFFECTIVE:
		length = cs_f71_debug_capture_locked(cs,
				entry->id == CS_F71_PROC_RAW ? 0x10 : 0x21,
				bytes, sizeof(bytes));
		if (length == -ETIMEDOUT) {
			seq_puts(seq, "Data is not ready yet!\n");
			return 0;
		}
		if (length < 0)
			return length;
		if (length < (entry->id == CS_F71_PROC_FORCE ? 4 : 8))
			return -EPROTO;
		if (entry->id == CS_F71_PROC_RAW) {
			seq_printf(seq, "D0 : %d, D1 : %d, D2 : %d, len : %d\n",
					bytes[0], bytes[1], bytes[2], length);
			for (i = 0; i < 4; i++)
				seq_printf(seq, "raw_data %d = %d\n", i,
						(s16)get_unaligned_le16(bytes + i * 2));
		} else if (entry->id == CS_F71_PROC_EFFECTIVE) {
			seq_printf(seq, "effic_forcevalue:D0 : %d, D1 : %d, D2 : %d, len : %d\n",
					bytes[0], bytes[1], bytes[2], length);
			for (i = 0; i < 4; i++)
				seq_printf(seq, "diff_data:%05d ",
						(s16)get_unaligned_le16(bytes + i * 2));
		} else {
			for (i = 0; i < 2; i++)
				seq_printf(seq, "%d ",
						(s16)get_unaligned_le16(bytes + i * 2));
		}
		seq_putc(seq, '\n');
		return 0;
	case CS_F71_PROC_COEFFICIENT:
		ret = cs_f71_rw_test_locked(cs, 0x67);
		if (!ret)
			ret = cs_f71_read(cs, 0x24, bytes, 8);
		if (ret) {
			seq_puts(seq, "read coefs fail!\n");
			return 0;
		}
		for (i = 0; i < 4; i++)
			seq_printf(seq, "%05d ", get_unaligned_be16(bytes + 2 * i));
		seq_putc(seq, '\n');
		return 0;
	case CS_F71_PROC_LEFT:
	case CS_F71_PROC_RIGHT:
		ret = cs_f71_read_gears_locked(cs, gears);
		if (ret)
			seq_puts(seq, "read trig err\n");
		else {
			i = entry->id == CS_F71_PROC_LEFT ? 0 : 2;
			seq_printf(seq, "%d %d\n", gears[i], gears[i + 1]);
		}
		return 0;
	case CS_F71_PROC_GAME:
	case CS_F71_PROC_SLEEP:
		ret = cs_f71_rw_test_locked(cs, 0x67);
		if (!ret)
			ret = cs_f71_read(cs, 0x55, bytes, 1);
		if (ret) {
			seq_puts(seq, entry->id == CS_F71_PROC_GAME ?
					"read game mode err\n" : "read sleep status err\n");
			return 0;
		}
		if (bytes[0] != 0 && bytes[0] != 2)
			return 0;
		if (entry->id == CS_F71_PROC_GAME)
			seq_puts(seq, bytes[0] ? "game mode\n" : "not game mode\n");
		else
			seq_puts(seq, bytes[0] ? "forcetouch not sleep\n" : "forcetouch is sleep\n");
		return 0;
	case CS_F71_PROC_CAL_DATA:
		ret = cs_f71_rw_test_locked(cs, 0x67);
		if (!ret)
			ret = cs_f71_read(cs, 0x52, bytes, 1);
		if (ret)
			seq_puts(seq, "read Calibration status err\n");
		else
			seq_printf(seq, "%d\n", bytes[0]);
		return 0;
	case CS_F71_PROC_FW_FILE:
	case CS_F71_PROC_TEST:
		/* Both original ops use the genuinely empty cs_proc_show. */
		return 0;
	default:
		return -EINVAL;
	}
}

static int cs_f71_proc_show(struct seq_file *seq, void *unused)
{
	struct cs_f71_proc *entry = seq->private;
	struct cs_f71 *cs = entry->cs;
	int ret;

	if (entry->id == CS_F71_PROC_FW_FORCE)
		return cs_f71_queue_firmware(cs, true);
	mutex_lock(&cs->operation_lock);
	if (entry->id == CS_F71_PROC_BOOT)
		cs_f71_irq_block(cs, CS_F71_IRQ_FIRMWARE);
	mutex_lock(&cs->command_lock);
	ret = cs->removed ? -ENODEV : cs_f71_proc_show_locked(seq, entry);
	mutex_unlock(&cs->command_lock);
	if (entry->id == CS_F71_PROC_BOOT)
		cs_f71_irq_unblock(cs, CS_F71_IRQ_FIRMWARE);
	mutex_unlock(&cs->operation_lock);
	return ret;
}

static int cs_f71_proc_open(struct inode *inode, struct file *file)
{
	struct cs_f71_proc *entry = PDE_DATA(inode);
	struct cs_f71 *cs = entry->cs;
	int ret;

	mutex_lock(&cs->command_lock);
	if (cs->removed) {
		mutex_unlock(&cs->command_lock);
		return -ENODEV;
	}
	kref_get(&cs->ref);
	mutex_unlock(&cs->command_lock);
	ret = single_open(file, cs_f71_proc_show, entry);
	if (ret)
		cs_f71_put(cs);
	return ret;
}

static int cs_f71_proc_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct cs_f71_proc *entry = seq->private;
	struct cs_f71 *cs = entry->cs;
	int ret = single_release(inode, file);

	cs_f71_put(cs);
	return ret;
}

static const struct proc_ops cs_f71_proc_ro_ops = {
	.proc_open = cs_f71_proc_open,
	.proc_read = seq_read,
	.proc_release = cs_f71_proc_release,
};

static const struct proc_ops cs_f71_proc_rw_ops = {
	.proc_open = cs_f71_proc_open,
	.proc_read = seq_read,
	.proc_write = cs_f71_proc_write,
	.proc_release = cs_f71_proc_release,
};

void cs_f71_proc_exit(struct cs_f71 *cs)
{
	proc_remove(cs->proc_root);
	cs->proc_root = NULL;
}

int cs_f71_proc_init(struct cs_f71 *cs)
{
	static const char * const names[] = {
		"fw_info", "read_boot_version", "fw_update_file", "rw_reg",
		"cs_iic_test", "read_rawdata", "read_cali_coef", "read_force_data",
		"read_effic_force_value", "force_update_fw", "left_press_trig",
		"right_press_trig", "press_game_switch", "sleep", "cal_data",
	};
	unsigned int i;
	bool writable;

	cs->proc_root = proc_mkdir("cs_press", NULL);
	if (!cs->proc_root)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		cs->proc_entries[i].cs = cs;
		cs->proc_entries[i].id = i;
		writable = (i >= CS_F71_PROC_BOOT && i <= CS_F71_PROC_TEST) ||
				(i >= CS_F71_PROC_LEFT && i <= CS_F71_PROC_SLEEP);
		if (!proc_create_data(names[i], 0666, cs->proc_root,
				writable ? &cs_f71_proc_rw_ops : &cs_f71_proc_ro_ops,
				&cs->proc_entries[i])) {
			cs_f71_proc_exit(cs);
			return -ENOMEM;
		}
	}
	return 0;
}
