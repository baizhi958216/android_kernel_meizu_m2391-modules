# M2391 SmartSwap source

GPL foundation: `tangalbert919/android_kernel_oneplus_sm8450`, commit
`e57861e7a086eb44b2b2c06c17e4c854b99b64db`,
`mm/oplus_mm/hybridswap_zram`. Original copyright and licensing notices are
preserved. This is the real compressed zram, backing-store extent manager,
asynchronous bio scheduler and reclaim daemon implementation.

The M2391 extension restores the original cgroup/sysfs interfaces, anonymous
MGLRU reclamation, daemon scan ratio, fault-first pauses, per-slot and
per-extent fault sharing, deferred extent reuse and five latency histograms.
The original `free_swap_is_low` export retains its naturally generated
`0xa0939f54` CRC, export kind and empty namespace. No CRC or version sections
are rewritten.

VM isolation lives in the native kernel's `meizu_smartshrink_anon()` and uses
its actual MGLRU isolation, page accounting, skip statistics and reclaim
implementation. The module does not duplicate `scan_control` or private
scheduler/runqueue structures. Memcg iteration pins both the CSS and its ID.

Reset withdraws new work, stops and joins producers, drains SRCU leases and
actual outstanding bio requests, then frees compressed pages while their
tracking metadata still exists. Manager teardown follows page cleanup.
Statistics remain allocated until device/cgroup files and trace hooks have
been removed. Per-extent completion state has additional private tail fields
for explicit error and deferred-free ownership; it is not an exported ABI.

Validation uses real Linux 5.15 Kbuild objects with FULL LTO and CFI, natural
export CRC comparison, original ELF/data/disassembly comparison and compiler
layout checks. Ten original private structures have 173 matching size/offset
records before the intentional fault-controller tail extension. Most GPL
foundation functions also match the original machine code. This does not
claim a byte-identical reconstruction: lifetime fixes, current kernel APIs
and diagnostics differ. Original systrace strings, CPU-capacity sampling and
extra slow-operation debug logs are not reproduced in full; I/O, statistics
and reclaim decisions are implemented in source.

Evidence and rollback copies are under `out/m2391-515-analysis/`. This work
does not constitute a hardware boot test or a completed ROM build.
