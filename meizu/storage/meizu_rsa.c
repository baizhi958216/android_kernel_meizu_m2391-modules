// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the BSD-style license in LICENSE.vboot.
 *
 * Montgomery arithmetic adapted from
 * external/vboot_reference/firmware/lib/cryptolib/rsa.c, which extends Android's
 * RSA implementation. Fixed key layout, exponent 3 support and verification
 * behavior recovered from M2391 meizu_private.ko .text 0xae4..0x157c.
 *
 * This intentionally preserves the original XOR-plus-SHA1 padding check, not
 * a guessed padding parser. The public key declaration reproduces the stock
 * wrapper's export CRC without overriding any version metadata.
 */
#include <crypto/sha1.h>
#include <linux/build_bug.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/string.h>
#include <asm/unaligned.h>

#include "meizu_storage.h"

#define MZ_RSA_WORDS 64
#define MZ_RSA_BYTES (MZ_RSA_WORDS * 4)

static void subM(const RSAPublicKey *key, uint32_t *a)
{
	int64_t A = 0;
	uint32_t i;

	for (i = 0; i < key->len; ++i) {
		A += (uint64_t)a[i] - key->n[i];
		a[i] = (uint32_t)A;
		A >>= 32;
	}
}

static int geM(const RSAPublicKey *key, const uint32_t *a)
{
	uint32_t i;

	for (i = key->len; i;) {
		--i;
		if (a[i] < key->n[i])
			return 0;
		if (a[i] > key->n[i])
			return 1;
	}
	return 1;
}

static void montMulAdd(const RSAPublicKey *key, uint32_t *c,
		const uint32_t a, const uint32_t *b)
{
	uint64_t A = (uint64_t)a * b[0] + c[0];
	uint32_t d0 = (uint32_t)A * key->n0inv;
	uint64_t B = (uint64_t)d0 * key->n[0] + (uint32_t)A;
	uint32_t i;

	for (i = 1; i < key->len; ++i) {
		A = (A >> 32) + (uint64_t)a * b[i] + c[i];
		B = (B >> 32) + (uint64_t)d0 * key->n[i] + (uint32_t)A;
		c[i - 1] = (uint32_t)B;
	}
	A = (A >> 32) + (B >> 32);
	c[i - 1] = (uint32_t)A;
	if (A >> 32)
		subM(key, c);
}

static void montMul(const RSAPublicKey *key, uint32_t *c,
		const uint32_t *a, const uint32_t *b)
{
	uint32_t i;

	for (i = 0; i < key->len; ++i)
		c[i] = 0;
	for (i = 0; i < key->len; ++i)
		montMulAdd(key, c, a[i], b);
}

/* No allocation or sleeping: the original verification API is synchronous. */
static void mz_sha1(const uint8_t *data, size_t len, uint8_t *digest)
{
	u32 state[SHA1_DIGEST_WORDS];
	u32 workspace[SHA1_WORKSPACE_WORDS];
	u8 tail[SHA1_BLOCK_SIZE * 2] = { 0 };
	u64 bits = (u64)len * 8;
	size_t final_size;
	unsigned int i;

	sha1_init(state);
	while (len >= SHA1_BLOCK_SIZE) {
		sha1_transform(state, (const char *)data, workspace);
		data += SHA1_BLOCK_SIZE;
		len -= SHA1_BLOCK_SIZE;
	}
	if (len)
		memcpy(tail, data, len);
	tail[len] = 0x80;
	final_size = len < 56 ? SHA1_BLOCK_SIZE : SHA1_BLOCK_SIZE * 2;
	put_unaligned_be64(bits, tail + final_size - 8);
	sha1_transform(state, (const char *)tail, workspace);
	if (final_size == SHA1_BLOCK_SIZE * 2)
		sha1_transform(state, (const char *)tail + SHA1_BLOCK_SIZE, workspace);
	for (i = 0; i < SHA1_DIGEST_WORDS; i++)
		put_unaligned_be32(state[i], digest + i * 4);
	memzero_explicit(state, sizeof(state));
	memzero_explicit(workspace, sizeof(workspace));
	memzero_explicit(tail, sizeof(tail));
}

int rsa_with_sha1_verify(const uint8_t *data, uint16_t len,
		const RSAPublicKey *key, const uint8_t *signature)
{
	static const u8 padding_hash[SHA1_DIGEST_SIZE] = {
		0xdc, 0xbd, 0xbe, 0x42, 0xd5, 0xf5, 0xa7, 0x2e, 0x6e, 0xfc,
		0xf5, 0x5d, 0xaf, 0x9d, 0xea, 0x68, 0x7c, 0xfb, 0xf1, 0x67,
	};
	u32 a[MZ_RSA_WORDS], aR[MZ_RSA_WORDS], aaR[MZ_RSA_WORDS];
	u32 *result;
	u8 block[MZ_RSA_BYTES], data_hash[SHA1_DIGEST_SIZE];
	u8 block_hash[SHA1_DIGEST_SIZE];
	unsigned int i;
	u8 difference = 0;
	int ret;

	BUILD_BUG_ON(sizeof(RSAPublicKey) != 524);
	if (!key || !signature || (!data && len) || key->len != MZ_RSA_WORDS ||
			(key->exponent != 3 && key->exponent != 65537))
		return -EINVAL;
	mz_sha1(data, len, data_hash);
	for (i = 0; i < MZ_RSA_WORDS; i++)
		a[i] = get_unaligned_be32(signature + (MZ_RSA_WORDS - 1 - i) * 4);
	montMul(key, aR, a, key->rr);
	if (key->exponent == 65537) {
		for (i = 0; i < 16; i += 2) {
			montMul(key, aaR, aR, aR);
			montMul(key, aR, aaR, aaR);
		}
		montMul(key, aaR, aR, a);
		result = aaR;
	} else {
		montMul(key, aaR, aR, aR);
		montMul(key, aR, aaR, a);
		result = aR;
	}
	if (geM(key, result))
		subM(key, result);
	for (i = 0; i < MZ_RSA_WORDS; i++)
		put_unaligned_be32(result[MZ_RSA_WORDS - 1 - i], block + i * 4);
	/* Original .text 0x10dc..0x129c: XOR expected digest into trailing bytes,
	 * then hash the whole recovered block and compare the fixed padding hash.
	 */
	for (i = 0; i < SHA1_DIGEST_SIZE; i++)
		block[MZ_RSA_BYTES - SHA1_DIGEST_SIZE + i] ^= data_hash[i];
	mz_sha1(block, sizeof(block), block_hash);
	for (i = 0; i < SHA1_DIGEST_SIZE; i++)
		difference |= block_hash[i] ^ padding_hash[i];
	ret = difference ? -EINVAL : 0;
	memzero_explicit(a, sizeof(a));
	memzero_explicit(aR, sizeof(aR));
	memzero_explicit(aaR, sizeof(aaR));
	memzero_explicit(block, sizeof(block));
	memzero_explicit(data_hash, sizeof(data_hash));
	memzero_explicit(block_hash, sizeof(block_hash));
	return ret;
}
EXPORT_SYMBOL(rsa_with_sha1_verify);
