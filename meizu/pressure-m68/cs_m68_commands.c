// SPDX-License-Identifier: GPL-2.0-only
/* M68 command mailbox transactions recovered from original .text+0x608..
 * 0xf94. These differ from the public F61's direct 0x90/0x91 protocol.
 */
#include <linux/delay.h>
#include <asm/unaligned.h>

#include "cs_m68.h"

#include <linux/ctype.h>

int cs_m68_parse_hex_tokens(const char *buffer, size_t count, u32 *values,
		const u8 *digits, size_t capacity)
{
	char token[11];
	size_t offset = 0, start, length, index = 0;
	int ret;

	while (offset < count && index < capacity) {
		while (offset < count && isspace(buffer[offset]))
			offset++;
		if (offset == count)
			break;
		start = offset;
		while (offset < count && !isspace(buffer[offset]))
			offset++;
		length = offset - start;
		if (length < 3 || length > digits[index] + 2 ||
		    length >= sizeof(token) || buffer[start] != '0' ||
		    buffer[start + 1] != 'x')
			return -EINVAL;
		memcpy(token, buffer + start, length);
		token[length] = '\0';
		ret = kstrtou32(token, 0, &values[index]);
		if (ret)
			return ret;
		index++;
	}
	return index ? index : -EINVAL;
}

static int cs_m68_mailbox_mode(struct cs_m68 *cs, u8 mode)
{
	u8 reset = 0;
	int ret = cs_m68_write(cs, 0x80, &reset, 1);

	return ret < 0 ? ret : cs_m68_write(cs, 0x80, &mode, 1);
}

static int cs_m68_set_pair(struct cs_m68 *cs, u8 mode, int down, int up,
		u8 channel)
{
	u8 payload[7] = { channel <= 1 ? channel : mode, 0 };
	u8 length = sizeof(payload);
	int i, ret;

	put_unaligned_le16(down, payload + 2);
	put_unaligned_le16(up, payload + 4);
	for (i = 0; i < 6; i++)
		payload[6] += payload[i];
	mutex_lock(&cs->command_lock);
	ret = cs_m68_mailbox_mode(cs, mode);
	if (ret >= 0)
		ret = cs_m68_write(cs, 0x82, payload, sizeof(payload));
	if (ret >= 0)
		ret = cs_m68_write(cs, 0x81, &length, 1);
	mutex_unlock(&cs->command_lock);
	return ret < 0 ? ret : 1;
}

static int cs_m68_get_pair(struct cs_m68 *cs, u8 mode, int *down, int *up,
		bool select_channel, u8 channel)
{
	u8 request[2] = { channel <= 1 ? channel : mode, 0 };
	u8 response[5] = { 0 }, length = sizeof(request);
	int ret;

	if (!down || !up)
		return -EINVAL;
	mutex_lock(&cs->command_lock);
	ret = cs_m68_mailbox_mode(cs, mode);
	if (ret >= 0 && select_channel)
		ret = cs_m68_write(cs, 0x82, request, sizeof(request));
	if (ret >= 0)
		ret = cs_m68_write(cs, 0x81, &length, 1);
	if (ret >= 0) {
		msleep(30);
		ret = cs_m68_read(cs, 0x81, response, sizeof(response));
	}
	if (ret >= 0 && response[0] == 4) {
		*down = (s16)get_unaligned_le16(response + 1);
		*up = (s16)get_unaligned_le16(response + 3);
		ret = 1;
	} else if (ret >= 0) {
		ret = 0;
	}
	mutex_unlock(&cs->command_lock);
	return ret;
}

int cs_m68_set_threshold(struct cs_m68 *cs, int down, int up, u8 channel)
{
	return cs_m68_set_pair(cs, 0x32, down, up, channel);
}

int cs_m68_get_threshold(struct cs_m68 *cs, int *down, int *up, u8 channel)
{
	return cs_m68_get_pair(cs, 0x33, down, up, true, channel);
}

int cs_m68_set_vibrate_time(struct cs_m68 *cs, int down, int up, u8 channel)
{
	/* The original only implements the vibration-time command on channel 0. */
	return channel ? 0 : cs_m68_set_pair(cs, 0x34, down, up, 0);
}

int cs_m68_get_vibrate_time(struct cs_m68 *cs, int *down, int *up)
{
	return cs_m68_get_pair(cs, 0x35, down, up, false, 0);
}

int cs_m68_set_calibration(struct cs_m68 *cs, u32 value, u8 channel)
{
	return cs_m68_set_pair(cs, 0x30, value & U16_MAX, value >> 16, channel);
}
