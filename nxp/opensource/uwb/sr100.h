/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef M2391_SR100_H
#define M2391_SR100_H
#include <linux/ioctl.h>
#include <linux/types.h>
/* Scalar arguments; the encoded u32 size is part of the shipping userspace ABI. */
#define SR100_SET_PWR _IOW(0xea, 1, __u32)
#define SR100_SET_FWD _IOW(0xea, 2, __u32)
#define SR100_COMPAT_NOOP _IOW(0xea, 4, __u32)
#endif
