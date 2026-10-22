// SPDX-License-Identifier: GPL-2.0-only
/* Original M2391 lookup protocol: transient probes resolve ordinary symbols,
 * while function lookup prefers the compiler's CFI jump-table entry. The
 * address is returned only after unregister_kprobe has drained the probe.
 * Callers must own the target's lifetime and use its real C prototype.
 */
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/string.h>

static int mzprobe_kp_pre_handler(struct kprobe *probe, struct pt_regs *regs)
{
	return 0;
}
NOKPROBE_SYMBOL(mzprobe_kp_pre_handler);

unsigned long mzprobe_lookup_name(const char *name)
{
	struct kprobe probe = {
		.symbol_name = name,
		.pre_handler = mzprobe_kp_pre_handler,
	};
	unsigned long address;

	if (!name || !*name || register_kprobe(&probe) < 0)
		return 0;
	address = (unsigned long)probe.addr;
	unregister_kprobe(&probe);
	return address;
}
EXPORT_SYMBOL(mzprobe_lookup_name);
NOKPROBE_SYMBOL(mzprobe_lookup_name);

unsigned long mzprobe_lookup_function(const char *name)
{
	char local[64] = {};
	char *symbol = local;
	size_t bytes;
	unsigned long address = 0;
	int length;

	if (!name || !*name || check_add_overflow(strlen(name),
					       sizeof(".cfi_jt"), &bytes))
		return 0;
	/* The stock allocation is strlen(name)+9, including one extra spare
	 * byte beyond the seven-character suffix and terminating NUL. */
	if (check_add_overflow(bytes, (size_t)1, &bytes))
		return 0;
	if (bytes > sizeof(local)) {
		symbol = kmalloc(bytes, GFP_KERNEL);
		if (!symbol) {
			pr_err("%s: alloc memory failed.\n", __func__);
			return 0;
		}
	}
	length = snprintf(symbol, bytes, "%s%s", name, ".cfi_jt");
	if (length < 0 || length >= bytes) {
		pr_err("%s: snprintf failed.\n", __func__);
		goto free;
	}
	address = mzprobe_lookup_name(symbol);
	if (!address)
		address = mzprobe_lookup_name(name);
free:
	if (symbol != local)
		kfree(symbol);
	return address;
}
EXPORT_SYMBOL(mzprobe_lookup_function);
NOKPROBE_SYMBOL(mzprobe_lookup_function);
MODULE_AUTHOR("chenyuwen1@meizu.com");
MODULE_LICENSE("GPL");
