/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GOODIX_MEIZU_H
#define _GOODIX_MEIZU_H
struct goodix_ts_core;
struct goodix_ts_event;
struct input_dev;
bool goodix_meizu_is_ready(struct goodix_ts_core *cd);
int goodix_meizu_init(struct goodix_ts_core *cd);
void goodix_meizu_exit(struct goodix_ts_core *cd);
int goodix_meizu_gesture(struct goodix_ts_core *cd, unsigned int mode);
int goodix_meizu_fod(struct goodix_ts_core *cd, bool enabled);
void goodix_meizu_normal_event(struct goodix_ts_core *cd,
		struct goodix_ts_event *event, bool invert_xy);
void goodix_meizu_lp_event(struct goodix_ts_core *cd,
		const struct goodix_ts_event *event);
int goodix_meizu_suspend(struct goodix_ts_core *cd);
bool goodix_meizu_resume(struct goodix_ts_core *cd);
void goodix_meizu_resume_complete(struct goodix_ts_core *cd);
void goodix_meizu_input_capabilities(struct input_dev *input);
#endif
