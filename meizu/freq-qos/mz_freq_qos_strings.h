/* SPDX-License-Identifier: GPL-2.0-only */
/* Exact original diagnostic source mappings and proc descriptions. */
struct source_mapping { const char *symbol; const char *module; };
static const struct source_mapping add_sources[] = {
	{ "input_boost_init [sched_walt]", "walt_input_boost" },
	{ "init_freq_qos_request [sched_walt]", "walt_qos" },
	{ "ufs_qcom_qos_init [ufs_qcom]", "ufs_qcom" },
	{ "freq_qos_request_init [msm_performance]", "perf-lock" },
	{ "cpufreq_cdev_register [qti_cpufreq_cdev]", "thermal" },
	{ "cpufreq_online", "policy_qos" },
	{ "__cpufreq_cooling_register", "cooling" },
	{ "cc_cooling_probe [cpu_voltage_cooling]", "cooling_qcom" },
	{ "[rmnet_shs]", "rmnet_shs" },
	{ "wcn_freq_pm_qos_init", "wcn_pm_qos" },
};
static const struct source_mapping update_sources[] = {
	{ "set_cpu_min_freq [msm_performance]", "perf-lock" },
	{ "set_cpu_max_freq [msm_performance]", "perf-lock" },
	{ "cb_thermal_boost_work [mz_thermal_boost]", "mz_thermal" },
	{ "cpufreq_cdev_set_state [qti_cpufreq_cdev]", "thermal" },
	{ "cd_reset_qos [mz_cpu_dancing]", "cpu_dancing" },
	{ "cd_do_boundary_change_work [mz_cpu_dancing]", "cpu_dancing" },
	{ "ufs_qcom_cpufreq_dwork [ufs_qcom]", "ufs_qcom" },
	{ "cpufreq_set_cur_state", "cooling" },
	{ "store_scaling_min_freq", "policy_qos" },
	{ "store_scaling_max_freq", "policy_qos" },
	{ "update_policy_online [sched_walt]", "walt_input_boost" },
	{ "__add_freq_qos_request [sched_walt]", "walt_qos" },
	{ "[rmnet_shs]", "rmnet_shs" },
};
static const char * const proc_descriptions[] = {
	"type: all, all the system cpufreq qos\n",
	"type: all_no_proc, all the system cpufreq qos without process info\n",
	"type: valid, skip the cpuinfo_max_freq/cpuinfo_min_freq\n",
	"type: valid, skip the cpuinfo_max_freq/cpuinfo_min_freq without process info\n",
};
