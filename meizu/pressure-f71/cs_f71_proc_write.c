// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of the F71 proc writers at 0x5584..0x6853. */
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include "cs_f71.h"

static int cs_f71_hex_tokens(char *text, u8 *bytes, size_t capacity)
{
	char *token;
	size_t count = 0;
	int ret;

	while ((token = strsep(&text, " \t\r\n"))) {
		if (!*token)
			continue;
		if (count == capacity || strlen(token) != 4 ||
		    token[0] != '0' || token[1] != 'x' ||
		    !isxdigit(token[2]) || !isxdigit(token[3]))
			return -EINVAL;
		ret = kstrtou8(token, 0, bytes + count);
		if (ret)
			return ret;
		count++;
	}
	return count;
}

static int cs_f71_register_command(struct cs_f71 *cs, char *text)
{
	u8 tokens[33], mode, length;
	u16 reg;
	unsigned int header;
	int count, ret;

	count = cs_f71_hex_tokens(text, tokens, ARRAY_SIZE(tokens));
	if (count < 0)
		return count;
	if (count < 3)
		return -EINVAL;
	mode = tokens[0];
	length = tokens[1];
	if (mode > 3 || !length)
		return -EINVAL;
	header = mode >= 2 ? 4 : 3;
	if (count < header || count != header + ((mode & 1) ? 0 : length))
		return -EINVAL;
	if ((mode & 1) && length > sizeof(cs->register_cache))
		return -EMSGSIZE;
	reg = mode >= 2 ? get_unaligned_be16(tokens + 2) : tokens[2];
	ret = cs_f71_wakeup_locked(cs);
	if (ret)
		return ret;
	cs->register_cache_length = 0;
	switch (mode) {
	case 0:
		return cs_f71_write(cs, reg, tokens + header, length);
	case 1:
		ret = cs_f71_read(cs, reg, cs->register_cache, length);
		break;
	case 2:
		return cs_f71_write_flash(cs, reg, tokens + header, length);
	case 3:
		ret = cs_f71_read_flash(cs, reg, cs->register_cache, length);
		break;
	default:
		return -EINVAL;
	}
	if (!ret)
		cs->register_cache_length = length;
	return ret;
}

ssize_t cs_f71_proc_write(struct file *file, const char __user *user,
		size_t count, loff_t *offset)
{
	struct seq_file *seq = file->private_data;
	struct cs_f71_proc *entry = seq->private;
	struct cs_f71 *cs = entry->cs;
	char *text;
	unsigned int value;
	u8 test;
	int ret;

	if (!count || count > 512)
		return -EINVAL;
	text = memdup_user_nul(user, count);
	if (IS_ERR(text))
		return PTR_ERR(text);
	if (entry->id == CS_F71_PROC_FW_FILE) {
		/* Stock accepts any first byte, with ASCII '2' selecting force. */
		ret = cs_f71_queue_firmware(cs, text[0] == '2');
		goto free;
	}
	mutex_lock(&cs->operation_lock);
	mutex_lock(&cs->command_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto unlock;
	}
	switch (entry->id) {
	case CS_F71_PROC_BOOT:
		/* Original cs_proc_write copies and discards this payload. */
		ret = 0;
		break;
	case CS_F71_PROC_REG:
		ret = cs_f71_register_command(cs, text);
		break;
	case CS_F71_PROC_TEST:
		ret = cs_f71_hex_tokens(text, &test, 1);
		if (ret == 1)
			ret = cs_f71_rw_test_locked(cs, test);
		else if (!ret)
			ret = -EINVAL;
		break;
	case CS_F71_PROC_LEFT:
	case CS_F71_PROC_RIGHT:
		ret = kstrtouint(text, 10, &value);
		if (!ret && value > U16_MAX)
			ret = -ERANGE;
		if (!ret)
			ret = cs_f71_set_gear_locked(cs,
					entry->id == CS_F71_PROC_RIGHT, value);
		break;
	case CS_F71_PROC_GAME:
	case CS_F71_PROC_SLEEP:
		ret = kstrtouint(text, 10, &value);
		if (!ret && value > 1)
			ret = -EINVAL;
		if (!ret)
			ret = cs_f71_set_game_locked(cs,
					entry->id == CS_F71_PROC_GAME ? value : !value);
		break;
	default:
		ret = -EACCES;
		break;
	}
unlock:
	mutex_unlock(&cs->command_lock);
	mutex_unlock(&cs->operation_lock);
free:
	kfree(text);
	return ret ? ret : count;
}
