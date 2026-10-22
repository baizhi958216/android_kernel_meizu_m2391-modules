// SPDX-License-Identifier: GPL-2.0-only
/*
 * M2391 MZKV protocol recovery. Original .text: write/read_value0xe54/0x10a8,
 * raw manager IO0x144c..0x16a4, key creation0x1b68, scan0x1e10..0x2240,
 * raw key decoder0x2240, block manager constructor0x2810.
 *
 * Each key occupies an 8192-byte record: 44-byte key header at record+0,
 * 4012-byte value header at record+4096. Both magic words are0x4d5a4b56.
 * CRC fields are zero while hashing the entire fixed-size header. The hash
 * is CRC32(seed0, no final inversion), followed by the stock 64-bit mixer.
 * private reserves64 1KiB slots before records; other managers reserve0.
 * max records is ((partition_bytes-reserved_bytes)>>13)-1 as in stock.
 * Scanning stops at the first invalid key; invalid values leave an empty
 * cached value. New key creation writes its header immediately; reads use
 * the cache and zero-pad the destination. Raw manager APIs start at byte0.
 *
 * Handles remain opaque; a mutex-protected list replaces the original private
 * hash table without exposing its internals. Length/overflow checks repair
 * original buffer overruns. Uses the recovered storage driver's real block
 * IO instead of resolving an unexported kernel function through mzprobe.
 */
#include <linux/blkdev.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "../storage/meizu_storage.h"
#include "meizu_key_value.h"

#define MZKV_MAGIC 0x4d5a4b56U
#define MZKV_KEY_MAX 31
#define MZKV_VALUE_MAX 4000
#define MZKV_RECORD_SIZE 8192

struct raw_key {
	__le32 magic, keylen, crc;
	char key[32];
};
struct raw_value {
	__le32 magic, size, crc;
	char value[MZKV_VALUE_MAX];
};
struct key_manager;
struct key_value {
	struct list_head list;
	struct kref ref;
	struct key_manager *manager;
	struct mutex lock;
	size_t id, len, size;
	char name[32];
	char *value;
};
struct key_manager {
	const char *name;
	size_t slots, maximum, used;
	u64 capacity;
	struct kref ref;
	struct mutex lock;
	struct list_head keys;
};
static struct key_manager *managers[4];
static DEFINE_MUTEX(managers_lock);

static u32 record_hash(const void *buffer, size_t size)
{
	const u8 *bytes = buffer;
	u64 hash = 0;
	u32 crc = 0;
	size_t i;
	int bit;

	for (i = 0; i < size; i++) {
		crc ^= bytes[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
	}
	hash = crc;
	hash += hash << 12;
	hash ^= hash >> 22;
	hash += hash << 4;
	hash ^= hash >> 9;
	hash += hash << 10;
	hash ^= hash >> 2;
	hash += hash << 7;
	return (u32)((hash >> 15) ^ (hash >> 3)) * 0x9e3779b1U;
}

static int manager_io(struct key_manager *manager, int write, u64 offset,
		void *buffer, size_t size)
{
	if (!manager || !buffer || offset > manager->capacity ||
			size > manager->capacity - offset || offset > S64_MAX)
		return -EINVAL;
	return sd_partition_rw(manager->name, write, offset, buffer, size);
}

static void manager_release(struct kref *ref)
{
	struct key_manager *manager = container_of(ref, struct key_manager, ref);

	kfree(manager);
}
void mzkv_put_manager(mzkv_manager_t *mgr)
{
	struct key_manager *manager = (struct key_manager *)mgr;

	if (manager)
		kref_put(&manager->ref, manager_release);
}
EXPORT_SYMBOL(mzkv_put_manager);
mzkv_manager_t *mzkv_get_manager(const char *name)
{
	struct key_manager *manager = NULL;
	size_t i;

	if (!name)
		return NULL;
	mutex_lock(&managers_lock);
	for (i = 0; i < ARRAY_SIZE(managers); i++) {
		if (managers[i] && !strncmp(managers[i]->name, name, 64)) {
			manager = managers[i];
			kref_get(&manager->ref);
			break;
		}
	}
	mutex_unlock(&managers_lock);
	return (mzkv_manager_t *)manager;
}
EXPORT_SYMBOL(mzkv_get_manager);

int mzkv_manager_write(mzkv_manager_t *mgr, const char *buffer, const size_t size)
{
	return manager_io((struct key_manager *)mgr, 1, 0, (void *)buffer, size);
}
EXPORT_SYMBOL(mzkv_manager_write);
int mzkv_manager_read(mzkv_manager_t *mgr, char *buffer, const size_t size)
{
	return manager_io((struct key_manager *)mgr, 0, 0, buffer, size);
}
EXPORT_SYMBOL(mzkv_manager_read);
int mzkv_manager_write_by_slotid(mzkv_manager_t *mgr, const size_t slotid,
		const char *buffer, const size_t size)
{
	struct key_manager *manager = (struct key_manager *)mgr;

	if (!manager || size > 1024 || slotid >= manager->slots)
		return -EINVAL;
	return manager_io(manager, 1, (u64)slotid * 1024, (void *)buffer, size);
}
EXPORT_SYMBOL(mzkv_manager_write_by_slotid);
int mzkv_manager_read_by_slotid(mzkv_manager_t *mgr, const size_t slotid,
		char *buffer, const size_t size)
{
	struct key_manager *manager = (struct key_manager *)mgr;

	if (!manager || size > 1024 || slotid >= manager->slots)
		return -EINVAL;
	return manager_io(manager, 0, (u64)slotid * 1024, buffer, size);
}
EXPORT_SYMBOL(mzkv_manager_read_by_slotid);

static u64 key_offset(struct key_manager *manager, size_t id)
{
	return (u64)manager->slots * 1024 + (u64)id * MZKV_RECORD_SIZE;
}
static void key_release(struct kref *ref)
{
	struct key_value *key = container_of(ref, struct key_value, ref);

	kfree(key->value);
	mzkv_put_manager((mzkv_manager_t *)key->manager);
	kfree(key);
}
void mzkv_put_key(mzkv_key_t *handle)
{
	struct key_value *key = (struct key_value *)handle;

	if (key)
		kref_put(&key->ref, key_release);
}
EXPORT_SYMBOL(mzkv_put_key);

/* Caller holds manager lock; cache owns one reference to every key. */
static struct key_value *allocate_key(struct key_manager *manager,
		const char *name, size_t len, size_t id)
{
	struct key_value *key = kzalloc(sizeof(*key), GFP_KERNEL);

	if (!key)
		return NULL;
	kref_init(&key->ref);
	kref_get(&manager->ref);
	key->manager = manager;
	mutex_init(&key->lock);
	key->id = id;
	key->len = len;
	memcpy(key->name, name, len);
	key->name[len] = '\0';
	return key;
}
mzkv_key_t *mzkv_get_key(mzkv_manager_t *mgr, const char *keyname)
{
	struct key_manager *manager = (struct key_manager *)mgr;
	struct key_value *key;
	struct raw_key raw = { .magic = cpu_to_le32(MZKV_MAGIC) };
	size_t len;

	if (!manager || !keyname)
		return NULL;
	len = strnlen(keyname, 33);
	if (len > MZKV_KEY_MAX)
		return NULL;
	mutex_lock(&manager->lock);
	list_for_each_entry(key, &manager->keys, list) {
		if (key->len == len && !memcmp(key->name, keyname, len + 1))
			goto found;
	}
	if (manager->used >= manager->maximum)
		goto none;
	key = allocate_key(manager, keyname, len, manager->used);
	if (!key)
		goto none;
	raw.keylen = cpu_to_le32(len);
	memcpy(raw.key, keyname, len);
	raw.crc = cpu_to_le32(record_hash(&raw, sizeof(raw)));
	if (manager_io(manager, 1, key_offset(manager, key->id), &raw, sizeof(raw))) {
		mzkv_put_key((mzkv_key_t *)key);
		goto none;
	}
	manager->used++;
	list_add_tail(&key->list, &manager->keys);
found:
	kref_get(&key->ref);
	mutex_unlock(&manager->lock);
	return (mzkv_key_t *)key;
none:
	mutex_unlock(&manager->lock);
	return NULL;
}
EXPORT_SYMBOL(mzkv_get_key);

int mzkv_read_value(mzkv_key_t *handle, char *value, const size_t valuesize)
{
	struct key_value *key = (struct key_value *)handle;
	size_t copied;

	if (!key || (!value && valuesize))
		return -EINVAL;
	mutex_lock(&key->lock);
	copied = min(valuesize, key->size);
	if (copied)
		memcpy(value, key->value, copied);
	if (valuesize > copied)
		memset(value + copied, 0, valuesize - copied);
	mutex_unlock(&key->lock);
	return 0;
}
EXPORT_SYMBOL(mzkv_read_value);
int mzkv_write_value(mzkv_key_t *handle, const char *value, const size_t valuesize)
{
	struct key_value *key = (struct key_value *)handle;
	struct raw_value *raw;
	char *copy = NULL;
	int ret;

	if (!key || valuesize > MZKV_VALUE_MAX || (!value && valuesize))
		return -EINVAL;
	raw = kzalloc(sizeof(*raw), GFP_KERNEL);
	if (!raw)
		return -ENOMEM;
	if (valuesize) {
		copy = kmemdup(value, valuesize, GFP_KERNEL);
		if (!copy) {
			kfree(raw);
			return -ENOMEM;
		}
	}
	raw->magic = cpu_to_le32(MZKV_MAGIC);
	raw->size = cpu_to_le32(valuesize);
	if (valuesize)
		memcpy(raw->value, value, valuesize);
	raw->crc = cpu_to_le32(record_hash(raw, sizeof(*raw)));
	mutex_lock(&key->lock);
	/* The original API updates its cache even if the later disk write fails. */
	kfree(key->value);
	key->value = copy;
	key->size = valuesize;
	ret = manager_io(key->manager, 1,
			key_offset(key->manager, key->id) + 4096, raw, sizeof(*raw));
	mutex_unlock(&key->lock);
	kfree(raw);
	return ret < 0 ? -EINVAL : 0;
}
EXPORT_SYMBOL(mzkv_write_value);

static int scan_keys(struct key_manager *manager)
{
	struct raw_key raw;
	struct raw_value *value;
	struct key_value *key;
	u32 stored_crc, size, len;
	size_t id;
	bool duplicate;
	int ret;

	value = kmalloc(sizeof(*value), GFP_KERNEL);
	if (!value)
		return -ENOMEM;
	for (id = 0; id < manager->maximum; id++) {
		ret = manager_io(manager, 0, key_offset(manager, id), &raw, sizeof(raw));
		if (ret)
			break;
		stored_crc = le32_to_cpu(raw.crc);
		raw.crc = 0;
		len = le32_to_cpu(raw.keylen);
		if (raw.magic != cpu_to_le32(MZKV_MAGIC) || len > MZKV_KEY_MAX ||
				stored_crc != record_hash(&raw, sizeof(raw)))
			break;
		raw.key[len] = '\0';
		/* Stock hashmap_put rejects later records with the same key. They
		 * still consume record IDs, but the first cached value wins.
		 */
		manager->used = id + 1;
		duplicate = false;
		list_for_each_entry(key, &manager->keys, list) {
			if (key->len == len && !memcmp(key->name, raw.key, len + 1)) {
				duplicate = true;
				break;
			}
		}
		if (duplicate)
			continue;
		key = allocate_key(manager, raw.key, len, id);
		if (!key) {
			kfree(value);
			return -ENOMEM;
		}
		list_add_tail(&key->list, &manager->keys);
		ret = manager_io(manager, 0, key_offset(manager, id) + 4096,
				value, sizeof(*value));
		if (ret)
			continue;
		stored_crc = le32_to_cpu(value->crc);
		value->crc = 0;
		size = le32_to_cpu(value->size);
		if (value->magic != cpu_to_le32(MZKV_MAGIC) || size > MZKV_VALUE_MAX ||
				stored_crc != record_hash(value, sizeof(*value)))
			continue;
		if (size) {
			key->value = kmemdup(value->value, size, GFP_KERNEL);
			if (key->value)
				key->size = size;
		}
	}
	kfree(value);
	return 0;
}

static void release_cached_keys(struct key_manager *manager)
{
	struct key_value *key, *next;

	list_for_each_entry_safe(key, next, &manager->keys, list) {
		list_del(&key->list);
		mzkv_put_key((mzkv_key_t *)key);
	}
	mzkv_put_manager((mzkv_manager_t *)manager);
}
static struct key_manager *build_manager(const char *name, size_t slots)
{
	struct key_manager *manager;
	struct block_device *bdev;
	char label[75];
	u64 capacity, records, reserved = (u64)slots * 1024;
	dev_t dev;
	int ret;

	snprintf(label, sizeof(label), "PARTLABEL=%s", name);
	dev = name_to_dev_t(label);
	if (!dev)
		return NULL;
	bdev = blkdev_get_by_dev(dev, FMODE_READ | FMODE_WRITE, NULL);
	if (IS_ERR(bdev))
		return NULL;
	capacity = i_size_read(bdev->bd_inode);
	blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
	if (capacity <= reserved)
		return NULL;
	records = (capacity - reserved) / MZKV_RECORD_SIZE;
	if (!records || records - 1 > SIZE_MAX)
		return NULL;
	manager = kzalloc(sizeof(*manager), GFP_KERNEL);
	if (!manager)
		return NULL;
	manager->name = name;
	manager->slots = slots;
	manager->maximum = records - 1;
	manager->capacity = capacity;
	kref_init(&manager->ref);
	mutex_init(&manager->lock);
	INIT_LIST_HEAD(&manager->keys);
	ret = scan_keys(manager);
	if (ret) {
		release_cached_keys(manager);
		return NULL;
	}
	return manager;
}
static int __init mzkv_init(void)
{
	static const char * const names[] = { "private", "camera", "rstinfo", "param" };
	size_t i;

	BUILD_BUG_ON(sizeof(struct raw_key) != 44);
	BUILD_BUG_ON(sizeof(struct raw_value) != 4012);
	for (i = 0; i < ARRAY_SIZE(managers); i++)
		managers[i] = build_manager(names[i], i ? 0 : 64);
	return 0;
}
static void __exit mzkv_exit(void)
{
	struct key_manager *old[ARRAY_SIZE(managers)];
	size_t i;

	mutex_lock(&managers_lock);
	memcpy(old, managers, sizeof(old));
	memset(managers, 0, sizeof(managers));
	mutex_unlock(&managers_lock);
	for (i = 0; i < ARRAY_SIZE(old); i++)
		if (old[i])
			release_cached_keys(old[i]);
}
module_init(mzkv_init);
module_exit(mzkv_exit);
MODULE_LICENSE("GPL v2");
