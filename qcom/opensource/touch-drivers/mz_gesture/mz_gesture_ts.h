/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MZ_GESTURE_TS_H
#define _MZ_GESTURE_TS_H

#include <linux/types.h>

struct input_dev;

extern int FOD_switch_enable;
extern int Gesture_switch_enable;
int mz_gesture_report(struct input_dev *input, unsigned int code, unsigned int value);
void mz_gesture_set_gesture_statu(bool state);
bool mz_gesture_get_gesture_statu(void);
void mz_gesture_handle_register(void (*handle)(uint8_t state));

#endif
