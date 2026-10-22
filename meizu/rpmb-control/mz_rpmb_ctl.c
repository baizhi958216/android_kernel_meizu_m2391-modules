// SPDX-License-Identifier: GPL-2.0-only
/* M2391 mz_rpmb_ctl recovery: stock .text ioctl 0x278..0xbf8,
 * private-slot routing 0x16c8..0x1a98, nonce/auth 0x1a98..0x20b0,
 * RAC validation 0x2138..0x2654. Split BTF supplies all wire layouts.
 * No persistent I/O occurs at registration. Every write below is reachable
 * only through its original explicit ioctl operation. Reject short user
 * copies (stock incorrectly checked only negative remaining-byte counts).
 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>
#include "../storage/meizu_storage.h"
#include "../device-info/mz_info.h"
#include "keys.h"

#define RPMB_IOCTL 0x2a761729U
#define RPMB_OPT_BASE 0x21560780U
#define SLOT_COM_MAGIC 0x434f4d4dU
#define SLOT_RECO_MAGIC 0x5245434fU
#define SLOT_SENS_MAGIC 0x53454e53U

struct rpmb_ctl_head { uint32_t opt; int32_t retval; };
struct rpmb_ctl_msg {
	struct rpmb_ctl_head head;
	union {
		uint8_t rsa_seed[20];
		uint8_t rsa_signature[256];
		uint32_t api_version, root_state, lock_state;
		int32_t verify_device;
	};
};
struct rpmb_slot_info {
	uint32_t slot_offset, reserve;
	uint8_t nonce[32];
	uint8_t buf[256], signature[256];
};
struct rpmb_slot_msg { struct rpmb_ctl_head head; struct rpmb_slot_info info; };
struct private_slot_info {
	struct rpmb_ctl_head head;
	uint32_t slot_magic, slot_number;
	uint8_t buf[2048];
};
static_assert(sizeof(struct rpmb_ctl_msg) == 264);
static_assert(sizeof(struct rpmb_slot_msg) == 560);
static_assert(sizeof(struct private_slot_info) == 2064);
static_assert(sizeof(RSAPublicKey) == 524);

/* Stock nonce is global, shared by all opens and consumed even on failure. */
static DEFINE_MUTEX(random_mutex);
static DEFINE_MUTEX(ctl_mutex);
static DEFINE_MUTEX(slot_mutex);
static uint8_t random_buffer[20];
static bool seed_available;

static int verify_challenge(const uint8_t signature[256], const RSAPublicKey *key)
{
	uint8_t seed[20];
	int rc;

	mutex_lock(&random_mutex);
	if (!seed_available) {
		mutex_unlock(&random_mutex);
		return -1;
	}
	seed_available = false;
	memcpy(seed, random_buffer, sizeof(seed));
	mutex_unlock(&random_mutex);
	rc = rsa_with_sha1_verify(seed, sizeof(seed), key, signature);
	memzero_explicit(seed, sizeof(seed));
	return rc;
}

static int write_state(unsigned int slot, unsigned int enable,
		unsigned int lock_state, bool clear)
{
	uint8_t *data = kzalloc(1024, GFP_KERNEL);
	int rc;

	if (!data)
		return -ENOMEM;
	if (!clear) {
		memset(data, 'Z', 64);
		/* BTF system_rtx: magic[64], product, enable, lock_state. */
		put_unaligned_le32(enable, data + 68);
		put_unaligned_le32(lock_state, data + 72);
	}
	rc = private_slot_rw(1, slot, data, 1024);
	kfree_sensitive(data);
	return rc;
}

static int read_rac(uint8_t rac[256])
{
	uint8_t *data = kzalloc(1024, GFP_KERNEL);
	unsigned int i;
	int rc;

	if (!data)
		return -ENOMEM;
	rc = private_slot_rw(0, 66, data, 1024);
	if (rc < 0)
		goto out;
	for (i = 0; i < 64; i++)
		if (data[i] != 'Z') {
			memset(rac, 0, 256);
			rc = -1;
			goto out;
		}
	memcpy(rac, data + 64, 256);
out:
	kfree_sensitive(data);
	return rc;
}

static int rac_is_valid(void)
{
	uint8_t chipid[64] = { 0 }, rac[256] = { 0 };
	const char *id;
	int rc;

	rc = read_rac(rac);
	if (rc < 0)
		return 0;
	id = meizu_get_chipid();
	if (!id)
		return 0;
	/* Exactly 48 bytes, NUL padded by strncpy, signed with rac_key. */
	strncpy((char *)chipid, id, 48);
	rc = rsa_with_sha1_verify(chipid, 48, &rac_key, rac);
	memzero_explicit(rac, sizeof(rac));
	return rc >= 0;
}

static int root_state(void)
{
	uint8_t *data;
	bool rac_valid = rac_is_valid();
	int rc;

	data = kzalloc(1024, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	rc = private_slot_rw(0, 16, data, 1024);
	if (rc >= 0) {
		if (is_secure_chip() && !bootloader_is_unlock() && !rac_valid)
			rc = 0;
		else
			rc = get_unaligned_le32(data + 68) == 1;
	}
	kfree_sensitive(data);
	return rc;
}

static int write_rac(const struct rpmb_slot_info *info)
{
	uint8_t chipid[64] = { 0 };
	uint8_t *data;
	const char *id;
	int rc;

	/* Stock authenticates info.buf, not info.signature or nonce. */
	if (memcmp(info->nonce, "RAC", 3))
		return -1;
	id = meizu_get_chipid();
	if (!id)
		return -1;
	strncpy((char *)chipid, id, 48);
	rc = rsa_with_sha1_verify(chipid, 48, &rac_key, info->buf);
	if (rc < 0)
		return rc;
	data = kzalloc(1024, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	memset(data, 'Z', 64);
	memcpy(data + 64, info->buf, 256);
	rc = private_slot_rw(1, 66, data, 1024);
	kfree_sensitive(data);
	return rc;
}

static int private_slot_operation(struct private_slot_info *info, bool write)
{
	uint8_t *data;
	unsigned int slot;
	size_t len;
	bool param = false;
	int rc;

	switch (info->slot_magic) {
	case SLOT_COM_MAGIC:
		if (info->slot_number < 64 || info->slot_number >= 512)
			return -1;
		slot = info->slot_number;
		len = 1024;
		break;
	case SLOT_RECO_MAGIC:
		slot = 263;
		len = 1024;
		break;
	case SLOT_SENS_MAGIC:
		slot = 512;
		len = 2048;
		param = true;
		break;
	default:
		return -1;
	}
	data = kzalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	if (write)
		memcpy(data, info->buf, len);
	rc = param ? param_slot_rw(write, slot, data, len) :
		private_slot_rw(write, slot, data, len);
	if (!write)
		memcpy(info->buf, data, len);
	kfree_sensitive(data);
	info->head.retval = rc;
	return rc;
}

static int control_operation(struct rpmb_ctl_msg *msg, uint32_t opt)
{
	uint8_t *data;
	unsigned int state;
	int rc = 0;

	switch (opt - RPMB_OPT_BASE) {
	case 0: /* unlock: signature over the single-use random challenge */
		rc = verify_challenge(msg->rsa_signature, &unlock_key);
		if (rc >= 0)
			rc = write_state(17, 0, 5, false);
		break;
	case 1: /* report lost; original operation does not require a signature */
		rc = write_state(17, 1, 4, false);
		break;
	case 2: /* lock */
		rc = write_state(17, 1, 3, false);
		break;
	case 3:
		mutex_lock(&random_mutex);
		get_random_bytes(random_buffer, sizeof(random_buffer));
		memcpy(msg->rsa_seed, random_buffer, sizeof(random_buffer));
		seed_available = true;
		mutex_unlock(&random_mutex);
		break;
	case 4:
		rc = verify_challenge(msg->rsa_signature, &root_key);
		if (rc >= 0)
			rc = write_state(16, 1, 0, false);
		break;
	case 5:
		rc = write_state(16, 0, 0, true);
		break;
	case 8:
		msg->api_version = 2;
		break;
	case 9:
		data = kzalloc(1024, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		rc = private_slot_rw(0, 17, data, 1024);
		if (rc >= 0) {
			state = get_unaligned_le32(data + 72);
			msg->lock_state = state == 3 ? 0x25570726U :
				state == 4 ? 0x25570725U : 0x25570722U;
		}
		kfree_sensitive(data);
		break;
	case 16:
		rc = root_state();
		if (rc >= 0) {
			msg->root_state = rc ? 0x25570730U : 0x25570739U;
			rc = 0;
		}
		break;
	case 34:
		/* Stock verify-device is explicitly a zero-valued query; RSA
		 * validation belongs to unlock/root/RAC operations above.
		 */
		msg->verify_device = 0;
		break;
	default:
		return -1;
	}
	msg->head.retval = rc;
	return rc;
}

static long rpmb_ctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rpmb_ctl_head head;
	void __user *user = (void __user *)arg;
	struct mutex *lock;
	void *msg;
	size_t size;
	uint32_t op;
	int rc;

	if (cmd != RPMB_IOCTL)
		return -1;
	if (copy_from_user(&head, user, sizeof(head)))
		return -EFAULT;
	op = head.opt - RPMB_OPT_BASE;
	switch (op) {
	case 6: case 7:
		size = sizeof(struct private_slot_info);
		lock = &slot_mutex;
		break;
	case 17: case 18: case 19:
		size = sizeof(struct rpmb_slot_msg);
		lock = &slot_mutex;
		break;
	case 0: case 1: case 2: case 3: case 4: case 5:
	case 8: case 9: case 16: case 34:
		size = sizeof(struct rpmb_ctl_msg);
		lock = &ctl_mutex;
		break;
	default:
		/* Stock logs an unknown operation and returns the successful
		 * header-copy result, without copying or changing any state.
		 */
		return 0;
	}
	msg = memdup_user(user, size);
	if (IS_ERR(msg))
		return PTR_ERR(msg);
	/* The first header selects the layout and authorization path. Refuse a
	 * changed operation in the complete snapshot before any state or I/O.
	 */
	if (((struct rpmb_ctl_head *)msg)->opt != head.opt) {
		kfree_sensitive(msg);
		return -EINVAL;
	}
	mutex_lock(lock);
	if (op == 6 || op == 7) {
		rc = private_slot_operation(msg, op == 6);
	} else if (op == 17 || op == 18 || op == 19) {
		struct rpmb_slot_msg *rac = msg;

		if (op == 17)
			rc = read_rac(rac->info.buf);
		else if (op == 18)
			rc = write_rac(&rac->info);
		else
			rc = write_state(66, 0, 0, true);
		rac->head.retval = rc;
	} else {
		rc = control_operation(msg, head.opt);
	}
	/* Negative provider/auth errors return directly, as stock does; the
	 * outgoing retval is copied only on a successful operation.
	 */
	if (rc >= 0)
		rc = copy_to_user(user, msg, size) ? -EFAULT : 0;
	mutex_unlock(lock);
	kfree_sensitive(msg);
	return rc;
}

static int rpmb_ctl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int rpmb_ctl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations rpmb_ctl_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = rpmb_ctl_ioctl,
	.open = rpmb_ctl_open,
	.release = rpmb_ctl_release,
};

static struct miscdevice rpmb_ctl_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mz_rpmb_ctl",
	.fops = &rpmb_ctl_fops,
};

static int __init rpmb_ctl_init(void)
{
	return misc_register(&rpmb_ctl_misc);
}

static void __exit rpmb_ctl_exit(void)
{
	misc_deregister(&rpmb_ctl_misc);
	memzero_explicit(random_buffer, sizeof(random_buffer));
}

module_init(rpmb_ctl_init);
module_exit(rpmb_ctl_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Meizu M2391 RPMB control ioctl interface");
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
