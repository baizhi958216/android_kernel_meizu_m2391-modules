/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_THERMAL_LOG_H
#define _MEIZU_THERMAL_LOG_H

/* Legacy stock ABI. Source providers should use the paired lifetime API below. */
extern unsigned int (*get_gpu_freq)(void);
extern unsigned int (*get_gpu_freq_max)(void);

/* Frequencies are MHz. Unregister sleeps until in-flight callbacks finish;
 * invoke it before destroying provider state, outside provider locks.
 * A callback must not unregister itself.
 */
int mz_thermal_register_gpu_callbacks(unsigned int (*current_cb)(void),
		unsigned int (*maximum)(void));
void mz_thermal_unregister_gpu_callbacks(unsigned int (*current_cb)(void),
		unsigned int (*maximum)(void));
#endif
