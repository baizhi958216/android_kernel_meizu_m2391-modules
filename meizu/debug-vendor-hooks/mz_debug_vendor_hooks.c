// SPDX-License-Identifier: GPL-2.0-only
/* M2391 signal diagnostics recovered from original ELF code and BTF. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <trace/hooks/signal.h>

static const char * const task_state_array[] = {
	"R", "S", "D", "T", "t", "X", "Z", "P", "I",
};

static const char *get_task_state(struct task_struct *task)
{
	unsigned int task_state = smp_load_acquire(&task->__state);
	unsigned int state = (task_state | READ_ONCE(task->exit_state)) &
		TASK_REPORT;

	if (task_state == TASK_IDLE)
		state = TASK_REPORT_IDLE;
	if (task_state == TASK_RTLOCK_WAIT)
		state = TASK_UNINTERRUPTIBLE;
	return task_state_array[fls(state)];
}

static void send_signal_handler(void *data, int sig,
		struct task_struct *cur, struct task_struct *dst)
{
	/* Original mask suppresses SIGUSR1, SIGSEGV, SIGPIPE and SIGCHLD. */
	if (sig > 31 || (sig >= 0 && sig <= 17 && (BIT(sig) & 0x22c00)))
		return;
	pr_info("MZ VendorHooks: [%d:%s] sig %d to [%d:%s] state=%s\n",
		cur->pid, cur->comm, sig, dst->pid, dst->comm,
		get_task_state(dst));
}

static int mz_debug_vendor_hooks_driver_probe(struct platform_device *pdev)
{
	int ret;

	ret = register_trace_android_vh_do_send_sig_info(send_signal_handler, NULL);
	if (ret)
		pr_err("MZ VendorHooks: register_trace_android_vh_do_send_sig_info fail, ret=%d\n",
			ret);
	return ret;
}

static int mz_debug_vendor_hooks_driver_remove(struct platform_device *pdev)
{
	unregister_trace_android_vh_do_send_sig_info(send_signal_handler, NULL);
	/* Drain in-flight callbacks before this module's code can be released. */
	tracepoint_synchronize_unregister();
	return 0;
}

static const struct of_device_id mz_debug_vendor_hooks_of_match[] = {
	{ .compatible = "meizu,debug-vendor-hooks" },
	{ }
};
MODULE_DEVICE_TABLE(of, mz_debug_vendor_hooks_of_match);

static struct platform_driver mz_debug_vendor_hooks_driver = {
	.probe = mz_debug_vendor_hooks_driver_probe,
	.remove = mz_debug_vendor_hooks_driver_remove,
	.driver = {
		.name = "meizu-debug-vendor-hooks",
		.of_match_table = mz_debug_vendor_hooks_of_match,
	},
};
module_platform_driver(mz_debug_vendor_hooks_driver);

MODULE_DESCRIPTION("MEIZU Debug Vendor Hooks Driver");
MODULE_LICENSE("GPL v2");
