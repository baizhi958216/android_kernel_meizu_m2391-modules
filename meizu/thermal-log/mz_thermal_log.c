// SPDX-License-Identifier: GPL-2.0-only
/* M2391 thermal logger recovered from stock .text, .rodata and BTF.
 * Retains the sysfs interface, sensor order, units and logging cadence.
 */
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/thermal.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include "../boot-mode/boot_mode.h"
#include "../system/meizu_sys.h"
#include "mz_thermal_log.h"

unsigned int (*get_gpu_freq)(void);
EXPORT_SYMBOL_GPL(get_gpu_freq);
unsigned int (*get_gpu_freq_max)(void);
EXPORT_SYMBOL_GPL(get_gpu_freq_max);
static DEFINE_MUTEX(gpu_callbacks_lock);
DEFINE_STATIC_SRCU(gpu_callbacks_srcu);

int mz_thermal_register_gpu_callbacks(unsigned int (*current_cb)(void),
		unsigned int (*maximum)(void))
{
	int ret = 0;

	if (!current_cb || !maximum)
		return -EINVAL;
	mutex_lock(&gpu_callbacks_lock);
	if (get_gpu_freq || get_gpu_freq_max)
		ret = -EBUSY;
	else {
		smp_store_release(&get_gpu_freq, current_cb);
		smp_store_release(&get_gpu_freq_max, maximum);
	}
	mutex_unlock(&gpu_callbacks_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mz_thermal_register_gpu_callbacks);

void mz_thermal_unregister_gpu_callbacks(unsigned int (*current_cb)(void),
		unsigned int (*maximum)(void))
{
	mutex_lock(&gpu_callbacks_lock);
	if (get_gpu_freq == current_cb && get_gpu_freq_max == maximum) {
		WRITE_ONCE(get_gpu_freq, NULL);
		WRITE_ONCE(get_gpu_freq_max, NULL);
		synchronize_srcu(&gpu_callbacks_srcu);
	}
	mutex_unlock(&gpu_callbacks_lock);
}
EXPORT_SYMBOL_GPL(mz_thermal_unregister_gpu_callbacks);

static const struct {
	const char *zone;
	char *link;
	const char *title;
} sensors[] = {
	{ "cpuss-2", "cpu_temp", "cpuT" },
	{ "gpuss-2", "gpu_temp", "gpuT" },
	{ "skin-msm-therm", "pcb_temp", "pcbT" },
	{ "wlan-therm", "wlantemp", "wlanT" },
	{ "pa-therm-1", "patemp", "paT" },
	{ "battery", "battemp", "batT" },
	{ "xo-therm", "xo_temp", "xoT" },
};
static const unsigned int policy_cpus[] = { 0, 3, 7 };
static const char * const columns[] = {
	"cpuT", "gpuT", "pcbT", "wlanT", "paT", "batT", "xoT",
	"p0-cur", "p3-cur", "p7-cur", "p0-max", "p3-max", "p7-max",
	"gpuF", "gpuM", "batI", "batV",
};

struct mz_thermal_log {
	struct kobject *kobj;
	struct kobj_attribute interval_attr;
	struct kobj_attribute helper_attr;
	struct delayed_work sample_work;
	struct delayed_work init_work;
	struct mutex lock;
	unsigned int interval;
	unsigned int interval_help;
	int bat_curr;
	int bat_vol;
	struct rtc_time tm_android;
	bool linked[ARRAY_SIZE(sensors)];
	bool ready;
	bool paused;
	bool stopping;
};

static int format_timestamp(struct mz_thermal_log *log, char *buf, size_t size)
{
	struct rtc_time *tm = &log->tm_android;

	return scnprintf(buf, size, "%d-%02d-%02d %02d:%02d:%02d, ",
			tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static int format_columns(struct mz_thermal_log *log, char *buf, size_t size)
{
	int i, n = format_timestamp(log, buf, size);

	for (i = 0; i < ARRAY_SIZE(columns); i++)
		n += scnprintf(buf + n, size - n, i == ARRAY_SIZE(columns) - 1 ?
				"%8s\n" : "%8s,", columns[i]);
	pr_info("[mz_thermal_log] %s", buf);
	return n;
}

static ssize_t interval_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct mz_thermal_log *log = container_of(attr,
			struct mz_thermal_log, interval_attr);

	return sysfs_emit(buf, "%d\n", READ_ONCE(log->interval));
}

static ssize_t interval_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct mz_thermal_log *log = container_of(attr,
			struct mz_thermal_log, interval_attr);
	unsigned int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;
	WRITE_ONCE(log->interval, value);
	return count;
}

static ssize_t helper_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct mz_thermal_log *log = container_of(attr,
			struct mz_thermal_log, helper_attr);
	int n;

	mutex_lock(&log->lock);
	n = format_columns(log, buf, PAGE_SIZE);
	mutex_unlock(&log->lock);
	n += sysfs_emit_at(buf, n,
		"==Thermal log help==\n"
		"cpuT  :   cpu temprature.\n"
		"gpuT  :   gpu temprature.\n"
		"pcbT  :   pcb temprature.\n"
		"xoT   :   external oscillator temprature.\n"
		"p0-cur:   policy 0 cur freqency.\n"
		"p3-cur:   policy 3 cur freqency.\n"
		"p7-cur:   policy 7 curfreqency.\n"
		"p0-max:   policy 0 max freqency.\n"
		"p3-max:   policy 3 max freqency.\n"
		"p7-max:   policy 7 max freqency.\n"
		"gpuF  :   gpu cur frequency.\n"
		"gpuM  :   gpu max frequencye.\n"
		"batI  :   battery current.\n"
		"batV  :   battery voitage.\n");
	return n;
}

static int read_temperature(const char *name)
{
	struct thermal_zone_device *zone;
	int temp;

	zone = thermal_zone_get_zone_by_name(name);
	if (IS_ERR_OR_NULL(zone) || thermal_zone_get_temp(zone, &temp))
		return -1;
	return temp;
}

static void sample_work(struct work_struct *work)
{
	struct mz_thermal_log *log = container_of(to_delayed_work(work),
			struct mz_thermal_log, sample_work);
	struct timespec64 now;
	struct cpufreq_policy *policy;
	struct power_supply *battery;
	union power_supply_propval prop;
	unsigned int (*current_cb)(void), (*maximum)(void);
	unsigned int interval;
	int values[ARRAY_SIZE(columns)], i, idx, n;
	char line[512];

	mutex_lock(&log->lock);
	if (log->stopping || log->paused)
		goto out;
	ktime_get_real_ts64(&now);
	rtc_time64_to_tm(now.tv_sec - (time64_t)sys_tz.tz_minuteswest * 60,
			&log->tm_android);
	for (i = 0; i < ARRAY_SIZE(sensors); i++)
		values[i] = read_temperature(sensors[i].zone);
	for (i = 0; i < ARRAY_SIZE(policy_cpus); i++) {
		policy = cpufreq_cpu_get(policy_cpus[i]);
		/* Stock uses unsigned division, including its missing-policy value. */
		values[7 + i] = (policy ? policy->cur : UINT_MAX) / 1000;
		values[10 + i] = (policy ? policy->max : UINT_MAX) / 1000;
		if (policy)
			cpufreq_cpu_put(policy);
	}
	idx = srcu_read_lock(&gpu_callbacks_srcu);
	current_cb = smp_load_acquire(&get_gpu_freq);
	maximum = smp_load_acquire(&get_gpu_freq_max);
	values[13] = current_cb ? current_cb() : -1;
	values[14] = maximum ? maximum() : -1;
	srcu_read_unlock(&gpu_callbacks_srcu, idx);
	battery = power_supply_get_by_name("battery");
	if (battery) {
		log->bat_curr = power_supply_get_property(battery,
				POWER_SUPPLY_PROP_CURRENT_NOW, &prop) ? -1 : prop.intval;
		log->bat_vol = power_supply_get_property(battery,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop) ? -1 : prop.intval;
		power_supply_put(battery);
	}
	/* Stock keeps the previous raw values when the supply is unavailable. */
	values[15] = log->bat_curr / 1000;
	values[16] = log->bat_vol / 1000;
	n = format_timestamp(log, line, sizeof(line));
	for (i = 0; i < ARRAY_SIZE(values); i++)
		n += scnprintf(line + n, sizeof(line) - n,
				i == ARRAY_SIZE(values) - 1 ? "%8d\n" : "%8d,",
				values[i]);
	pr_info("[mz_thermal_log] %s", line);
	interval = READ_ONCE(log->interval);
	log->interval_help += interval;
	if (log->interval_help >= 300000) {
		log->interval_help = 0;
		format_columns(log, line, sizeof(line));
	}
	/* A zero interval is accepted by stock. Keep a scheduler tick minimum
	 * rather than letting a sysfs write create an unbounded immediate loop.
	 */
	queue_delayed_work(system_wq, &log->sample_work,
			max_t(unsigned long, 1, msecs_to_jiffies(interval)));
out:
	mutex_unlock(&log->lock);
}

static void init_work(struct work_struct *work)
{
	struct mz_thermal_log *log = container_of(to_delayed_work(work),
			struct mz_thermal_log, init_work);
	struct thermal_zone_device *zones[ARRAY_SIZE(sensors)];
	int i;

	mutex_lock(&log->lock);
	if (log->stopping || log->paused)
		goto out;
	for (i = 0; i < ARRAY_SIZE(sensors); i++) {
		zones[i] = thermal_zone_get_zone_by_name(sensors[i].zone);
		if (IS_ERR_OR_NULL(zones[i])) {
			queue_delayed_work(system_wq, &log->init_work, HZ);
			goto out;
		}
	}
	for (i = 0; i < ARRAY_SIZE(sensors); i++) {
		if (!log->linked[i] &&
		    !meizu_sysfslink_register_name(&zones[i]->device, sensors[i].link))
			log->linked[i] = true;
	}
	log->ready = true;
	queue_delayed_work(system_wq, &log->sample_work, 0);
out:
	mutex_unlock(&log->lock);
}

static int thermal_log_probe(struct platform_device *pdev)
{
	struct mz_thermal_log *log;
	int ret;

	log = devm_kzalloc(&pdev->dev, sizeof(*log), GFP_KERNEL);
	if (!log)
		return -ENOMEM;
	mutex_init(&log->lock);
	log->interval = 10000;
	INIT_DELAYED_WORK(&log->sample_work, sample_work);
	INIT_DELAYED_WORK(&log->init_work, init_work);
	log->interval_attr = (struct kobj_attribute)
		__ATTR(interval, 0644, interval_show, interval_store);
	log->helper_attr = (struct kobj_attribute)
		__ATTR(helper, 0444, helper_show, NULL);
	log->kobj = meizu_sysfslink_get_kobj("mz_thermal_log");
	if (!log->kobj)
		return -ENOMEM;
	ret = sysfs_create_file(log->kobj, &log->interval_attr.attr);
	if (ret)
		goto put_kobj;
	ret = sysfs_create_file(log->kobj, &log->helper_attr.attr);
	if (ret) {
		sysfs_remove_file(log->kobj, &log->interval_attr.attr);
		goto put_kobj;
	}
	platform_set_drvdata(pdev, log);
	queue_delayed_work(system_wq, &log->init_work, HZ);
	return 0;
put_kobj:
	kobject_put(log->kobj);
	return ret;
}

static int thermal_log_remove(struct platform_device *pdev)
{
	struct mz_thermal_log *log = platform_get_drvdata(pdev);
	int i;

	mutex_lock(&log->lock);
	log->stopping = true;
	mutex_unlock(&log->lock);
	cancel_delayed_work_sync(&log->init_work);
	cancel_delayed_work_sync(&log->sample_work);
	sysfs_remove_file(log->kobj, &log->interval_attr.attr);
	sysfs_remove_file(log->kobj, &log->helper_attr.attr);
	for (i = 0; i < ARRAY_SIZE(sensors); i++)
		if (log->linked[i])
			meizu_sysfslink_remove_name(sensors[i].link);
	kobject_put(log->kobj);
	return 0;
}

static int thermal_log_suspend(struct device *dev)
{
	struct mz_thermal_log *log = dev_get_drvdata(dev);

	mutex_lock(&log->lock);
	log->paused = true;
	mutex_unlock(&log->lock);
	cancel_delayed_work_sync(&log->init_work);
	cancel_delayed_work_sync(&log->sample_work);
	return 0;
}

static int thermal_log_resume(struct device *dev)
{
	struct mz_thermal_log *log = dev_get_drvdata(dev);

	mutex_lock(&log->lock);
	log->paused = false;
	if (log->ready)
		queue_delayed_work(system_wq, &log->sample_work,
			max_t(unsigned long, 1, msecs_to_jiffies(READ_ONCE(log->interval))));
	else
		queue_delayed_work(system_wq, &log->init_work, HZ);
	mutex_unlock(&log->lock);
	return 0;
}

static SIMPLE_DEV_PM_OPS(thermal_log_pm, thermal_log_suspend, thermal_log_resume);
static const struct of_device_id thermal_log_match[] = {
	{ .compatible = "qcom,thermal-log" },
	{ }
};
MODULE_DEVICE_TABLE(of, thermal_log_match);
static struct platform_driver thermal_log_driver = {
	.probe = thermal_log_probe,
	.remove = thermal_log_remove,
	.driver = {
		.name = "thermal-log",
		.of_match_table = thermal_log_match,
		.pm = &thermal_log_pm,
	},
};
static bool driver_registered;
static int __init thermal_log_init(void)
{
	int ret;

	/* Original skips recovery (2), despite its generic NORMAL-only message. */
	if (get_boot_mode() == 2)
		return 0;
	ret = platform_driver_register(&thermal_log_driver);
	if (!ret)
		driver_registered = true;
	return ret;
}
static void __exit thermal_log_exit(void)
{
	if (driver_registered)
		platform_driver_unregister(&thermal_log_driver);
}
module_init(thermal_log_init);
module_exit(thermal_log_exit);
MODULE_DESCRIPTION("Meizu thermal telemetry logger");
MODULE_LICENSE("GPL v2");
