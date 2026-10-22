// SPDX-License-Identifier: GPL-2.0-only
/* M2391 ST NFC secure-state query, recovered from the original module.
 * ClientEnv opens peripheral-state application 0x108 and queries NFC 0x506.
 */
#define DEBUG
#include <linux/types.h>
#include <linux/printk.h>
#include "smcinvoke.h"
#include "smcinvoke_object.h"
#include "IClientEnv.h"

bool nfc_hw_secure_check(void)
{
	struct Object client, app;
	union ObjectArg args[2] = { { { 0, 0 } } };
	u32 uid = 0x506;
	u8 state = 0;
	bool secure = true;
	int rc;

	rc = get_client_env_object(&client);
	if (rc) {
		pr_err("Failed to get client_env_object, ret: %d\n", rc);
		return true;
	}
	rc = IClientEnv_open(client, 0x108, &app);
	if (rc) {
		pr_debug("Failed to get app_object, ret: %d\n", rc);
		if (rc == 12) {
			pr_debug("Secure HW feature not supported\n");
			secure = false;
		}
		goto release_client;
	}
	args[0].b = (struct ObjectBuf) { &uid, sizeof(uid) };
	args[1].b = (struct ObjectBuf) { &state, sizeof(state) };
	rc = Object_invoke(app, 1, args, ObjectCounts_pack(1, 1, 0, 0));
	pr_info("SMC invoke ret: %d state: %d\n", rc, state);
	if (!rc)
		secure = state == 1;
	else if (rc == 10) {
		pr_debug("Secure HW mode is not updated. Peripheral not found\n");
		secure = false;
	}
	Object_release(app);
release_client:
	Object_release(client);
	return secure;
}
