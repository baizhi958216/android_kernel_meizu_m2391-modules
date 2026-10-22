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

/* Original mincrypt layout and array expression, confirmed by stock BTF and
 * export CRC 0x8159496a. Preserve the sizeof expression in the public type:
 * genksyms distinguishes it from a literal 64 despite identical layout.
 */
#define MZ_RSA_NUMBYTES 256
#define MZ_RSA_NUMWORDS (MZ_RSA_NUMBYTES / sizeof(uint32_t))
typedef struct RSAPublicKey {
	int len;
	uint32_t n0inv;
	uint32_t n[MZ_RSA_NUMWORDS];
	uint32_t rr[MZ_RSA_NUMWORDS];
	int exponent;
} RSAPublicKey;

int rsa_with_sha1_verify(const uint8_t *data, uint16_t len,
		const RSAPublicKey *key, const uint8_t *signature);
#endif
