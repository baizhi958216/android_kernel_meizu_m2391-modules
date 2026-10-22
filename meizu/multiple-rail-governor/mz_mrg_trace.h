/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mrg
#if !defined(_MEIZU_MRG_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MEIZU_MRG_TRACE_H
#include <linux/tracepoint.h>
TRACE_EVENT(mrg_notify_nb,
	TP_PROTO(int cpu, unsigned long freq), TP_ARGS(cpu, freq),
	TP_STRUCT__entry(__field(int, cpu) __field(unsigned long, freq)),
	TP_fast_assign(__entry->cpu = cpu; __entry->freq = freq;),
	TP_printk("policy=%d freq=%ld", __entry->cpu, __entry->freq));
TRACE_EVENT(mrg_notify_state_change,
	TP_PROTO(int cpu, int new_state, int old_state),
	TP_ARGS(cpu, new_state, old_state),
	TP_STRUCT__entry(__field(int, cpu) __field(int, new_state)
		__field(int, old_state)),
	TP_fast_assign(__entry->cpu = cpu; __entry->new_state = new_state;
		__entry->old_state = old_state;),
	TP_printk("policy=%d new_state=%d old_state=%u", __entry->cpu,
		__entry->new_state, __entry->old_state));
TRACE_EVENT(mrg_freq_update,
	TP_PROTO(int cpu, unsigned int target_freq, unsigned int old_freq),
	TP_ARGS(cpu, target_freq, old_freq),
	TP_STRUCT__entry(__field(int, cpu) __field(unsigned int, target_freq)
		__field(unsigned int, old_freq)),
	TP_fast_assign(__entry->cpu = cpu; __entry->target_freq = target_freq;
		__entry->old_freq = old_freq;),
	TP_printk("policy=%d target_freq=%u old_freq=%u", __entry->cpu,
		__entry->target_freq, __entry->old_freq));
#endif
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mz_mrg_trace
#include <trace/define_trace.h>
