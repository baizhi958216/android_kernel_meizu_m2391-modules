/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM compaction_opt
#if !defined(_MEIZU_COMPACTION_OPT_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MEIZU_COMPACTION_OPT_TRACE_H
#include <linux/tracepoint.h>
#include <trace/events/mmflags.h>

TRACE_EVENT(compaction_opt_fix_extrfrag,
	TP_PROTO(struct zone *zone, unsigned long nr_all, unsigned long nr_high,
		unsigned long nr_all_trivial, unsigned long nr_high_trivial,
		unsigned int order),
	TP_ARGS(zone, nr_all, nr_high, nr_all_trivial, nr_high_trivial, order),
	TP_STRUCT__entry(
		__field(enum zone_type, idx)
		__field(unsigned long, nr_all)
		__field(unsigned long, nr_high)
		__field(unsigned long, nr_all_trivial)
		__field(unsigned long, nr_high_trivial)
		__field(unsigned long, score)
		__field(unsigned long, fix_score)
		__field(unsigned int, order)
	),
	TP_fast_assign(
		__entry->idx = zone_idx(zone);
		__entry->nr_all = nr_all;
		__entry->nr_high = nr_high;
		__entry->nr_all_trivial = nr_all_trivial;
		__entry->nr_high_trivial = nr_high_trivial;
		__entry->order = order;
		/* AArch64 stock UDIV yields zero for a zero divisor. Express that
		 * behavior explicitly rather than relying on C undefined behavior. */
		__entry->score = nr_all ? (nr_all - nr_high) * 100 / nr_all : 0;
		__entry->fix_score = nr_all != nr_all_trivial ?
			(nr_all - nr_all_trivial + nr_high_trivial - nr_high) * 100 /
			(nr_all - nr_all_trivial) : 0;
	),
	TP_printk("zone=%-8s nr_all=%ld nr_high=%ld nr_all_trivial=%ld nr_high_trivial=%ld order=%d score=%ld fix_score=%ld",
		__print_symbolic(__entry->idx, ZONE_TYPE), __entry->nr_all,
		__entry->nr_high, __entry->nr_all_trivial, __entry->nr_high_trivial,
		__entry->order, __entry->score, __entry->fix_score)
);
#endif
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE compaction_opt_trace
#include <trace/define_trace.h>
