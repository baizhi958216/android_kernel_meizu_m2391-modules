/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_KEY_VALUE_H
#define _MEIZU_KEY_VALUE_H
#include <linux/types.h>
/* The original API uses pointers to opaque unsigned-long handle typedefs. */
typedef unsigned long mzkv_manager_t;
typedef unsigned long mzkv_key_t;
mzkv_manager_t *mzkv_get_manager(const char *name);
void mzkv_put_manager(mzkv_manager_t *mgr);
mzkv_key_t *mzkv_get_key(mzkv_manager_t *mgr, const char *keyname);
void mzkv_put_key(mzkv_key_t *key);
int mzkv_write_value(mzkv_key_t *key, const char *value, const size_t valuesize);
int mzkv_read_value(mzkv_key_t *key, char *value, const size_t valuesize);
int mzkv_manager_write(mzkv_manager_t *mgr, const char *buffer, const size_t size);
int mzkv_manager_read(mzkv_manager_t *mgr, char *buffer, const size_t size);
int mzkv_manager_write_by_slotid(mzkv_manager_t *mgr, const size_t slotid,
		const char *buffer, const size_t size);
int mzkv_manager_read_by_slotid(mzkv_manager_t *mgr, const size_t slotid,
		char *buffer, const size_t size);
#endif
