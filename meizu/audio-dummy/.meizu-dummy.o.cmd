cmd_/home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/meizu-dummy.o := clang -Wp,-MMD,/home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/.meizu-dummy.o.d -nostdinc -isystem /home/zhi/derp/prebuilts/clang/kernel/linux-x86/clang-r450784e/lib64/clang/14.0.7/include -I/home/zhi/derp/kernel/meizu/m2391/arch/arm64/include -I./arch/arm64/include/generated -I/home/zhi/derp/kernel/meizu/m2391/include -I./include -I/home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi -I./arch/arm64/include/generated/uapi -I/home/zhi/derp/kernel/meizu/m2391/include/uapi -I./include/generated/uapi -include /home/zhi/derp/kernel/meizu/m2391/include/linux/compiler-version.h -include /home/zhi/derp/kernel/meizu/m2391/include/linux/kconfig.h -include /home/zhi/derp/kernel/meizu/m2391/include/linux/compiler_types.h -D__KERNEL__ -mlittle-endian -DKASAN_SHADOW_SCALE_SHIFT= -Qunused-arguments -fmacro-prefix-map=/home/zhi/derp/kernel/meizu/m2391/= -Wall -Wundef -Werror=strict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Wno-format-security -std=gnu89 --target=aarch64-linux-gnu -fintegrated-as -Werror=unknown-warning-option -Werror=ignored-optimization-argument -mgeneral-regs-only -DCONFIG_CC_HAS_K_CONSTRAINT=1 -Wno-psabi -fno-asynchronous-unwind-tables -fno-unwind-tables -mbranch-protection=pac-ret+leaf+bti -Wa,-march=armv8.5-a -DARM64_ASM_ARCH='"armv8.5-a"' -ffixed-x18 -DKASAN_SHADOW_SCALE_SHIFT= -fno-delete-null-pointer-checks -Wno-frame-address -Wno-address-of-packed-member -O2 -Wframe-larger-than=2048 -fstack-protector-strong -Werror -Wno-gnu -mno-global-merge -Wno-unused-but-set-variable -Wno-unused-const-variable -fno-omit-frame-pointer -fno-optimize-sibling-calls -ftrivial-auto-var-init=zero -enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang -fno-stack-clash-protection -g -gdwarf-4 -fsanitize=shadow-call-stack -fno-lto -flto -fvisibility=default -fsanitize=cfi -fsanitize-cfi-cross-dso -fno-sanitize-cfi-canonical-jump-tables -fno-sanitize-trap=cfi -fno-sanitize-blacklist -Wdeclaration-after-statement -Wvla -Wno-pointer-sign -Wno-array-bounds -fno-strict-overflow -fno-stack-check -Werror=date-time -Werror=incompatible-pointer-types -Wno-initializer-overrides -Wno-format -Wno-sign-compare -Wno-format-zero-length -Wno-pointer-to-enum-cast -Wno-tautological-constant-out-of-range-compare -Wno-unaligned-access -mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=1504  -fsanitize=array-bounds -fsanitize=local-bounds -fsanitize-undefined-trap-on-error  -DMODULE  -DKBUILD_BASENAME='"meizu_dummy"' -DKBUILD_MODNAME='"dummy_dlkm"' -D__KBUILD_MODNAME=kmod_dummy_dlkm -c -o /home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/meizu-dummy.o /home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/meizu-dummy.c

source_/home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/meizu-dummy.o := /home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/meizu-dummy.c

deps_/home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/meizu-dummy.o := \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/compiler-version.h \
    $(wildcard include/config/CC_VERSION_TEXT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kconfig.h \
    $(wildcard include/config/CPU_BIG_ENDIAN) \
    $(wildcard include/config/BOOGER) \
    $(wildcard include/config/FOO) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/compiler_types.h \
    $(wildcard include/config/HAVE_ARCH_COMPILER_H) \
    $(wildcard include/config/CC_HAS_ASM_INLINE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/compiler_attributes.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/compiler-clang.h \
    $(wildcard include/config/ARCH_USE_BUILTIN_BSWAP) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/compiler.h \
    $(wildcard include/config/CFI_CLANG) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/module.h \
    $(wildcard include/config/MODULES) \
    $(wildcard include/config/SYSFS) \
    $(wildcard include/config/MODULES_TREE_LOOKUP) \
    $(wildcard include/config/LIVEPATCH) \
    $(wildcard include/config/STACKTRACE_BUILD_ID) \
    $(wildcard include/config/GENERIC_BUG) \
    $(wildcard include/config/KALLSYMS) \
    $(wildcard include/config/SMP) \
    $(wildcard include/config/TRACEPOINTS) \
    $(wildcard include/config/TREE_SRCU) \
    $(wildcard include/config/BPF_EVENTS) \
    $(wildcard include/config/DEBUG_INFO_BTF_MODULES) \
    $(wildcard include/config/JUMP_LABEL) \
    $(wildcard include/config/TRACING) \
    $(wildcard include/config/EVENT_TRACING) \
    $(wildcard include/config/FTRACE_MCOUNT_RECORD) \
    $(wildcard include/config/KPROBES) \
    $(wildcard include/config/HAVE_STATIC_CALL_INLINE) \
    $(wildcard include/config/PRINTK_INDEX) \
    $(wildcard include/config/MODULE_UNLOAD) \
    $(wildcard include/config/CONSTRUCTORS) \
    $(wildcard include/config/FUNCTION_ERROR_INJECTION) \
    $(wildcard include/config/RETPOLINE) \
    $(wildcard include/config/MODULE_SIG) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/list.h \
    $(wildcard include/config/DEBUG_LIST) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/types.h \
    $(wildcard include/config/HAVE_UID16) \
    $(wildcard include/config/UID16) \
    $(wildcard include/config/ARCH_DMA_ADDR_T_64BIT) \
    $(wildcard include/config/PHYS_ADDR_T_64BIT) \
    $(wildcard include/config/64BIT) \
    $(wildcard include/config/ARCH_32BIT_USTAT_F_TINODE) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/types.h \
  arch/arm64/include/generated/uapi/asm/types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/int-ll64.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/int-ll64.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/bitsperlong.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitsperlong.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/bitsperlong.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/posix_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/stddef.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/stddef.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/posix_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/posix_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/poison.h \
    $(wildcard include/config/ILLEGAL_POINTER_VALUE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/const.h \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/const.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/const.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kernel.h \
    $(wildcard include/config/PREEMPT_VOLUNTARY) \
    $(wildcard include/config/PREEMPT_DYNAMIC) \
    $(wildcard include/config/PREEMPT_) \
    $(wildcard include/config/DEBUG_ATOMIC_SLEEP) \
    $(wildcard include/config/MMU) \
    $(wildcard include/config/PROVE_LOCKING) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/stdarg.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/align.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/limits.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/limits.h \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/limits.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/linkage.h \
    $(wildcard include/config/ARCH_USE_SYM_ANNOTATIONS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/stringify.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/export.h \
    $(wildcard include/config/MODVERSIONS) \
    $(wildcard include/config/MODULE_REL_CRCS) \
    $(wildcard include/config/HAVE_ARCH_PREL32_RELOCATIONS) \
    $(wildcard include/config/TRIM_UNUSED_KSYMS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/compiler.h \
    $(wildcard include/config/TRACE_BRANCH_PROFILING) \
    $(wildcard include/config/PROFILE_ALL_BRANCHES) \
    $(wildcard include/config/STACK_VALIDATION) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/rwonce.h \
    $(wildcard include/config/LTO) \
    $(wildcard include/config/AS_HAS_LDAPR) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/alternative-macros.h \
  arch/arm64/include/generated/asm/cpucaps.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/insn-def.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/rwonce.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kasan-checks.h \
    $(wildcard include/config/KASAN_GENERIC) \
    $(wildcard include/config/KASAN_SW_TAGS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kcsan-checks.h \
    $(wildcard include/config/KCSAN) \
    $(wildcard include/config/KCSAN_IGNORE_ATOMICS) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/linkage.h \
    $(wildcard include/config/ARM64_BTI_KERNEL) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/bitops.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/bits.h \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/bits.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/build_bug.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/typecheck.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/kernel.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/sysinfo.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/bitops.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/builtin-__ffs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/builtin-ffs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/builtin-__fls.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/builtin-fls.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/ffz.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/fls64.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/find.h \
    $(wildcard include/config/GENERIC_FIND_FIRST_BIT) \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/sched.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/hweight.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/arch_hweight.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/const_hweight.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/atomic.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/atomic.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/atomic.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/barrier.h \
    $(wildcard include/config/ARM64_PSEUDO_NMI) \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/barrier.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/cmpxchg.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/lse.h \
    $(wildcard include/config/ARM64_LSE_ATOMICS) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/atomic_ll_sc.h \
    $(wildcard include/config/CC_HAS_K_CONSTRAINT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/jump_label.h \
    $(wildcard include/config/HAVE_ARCH_JUMP_LABEL_RELATIVE) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/jump_label.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/insn.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/alternative.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/init.h \
    $(wildcard include/config/STRICT_KERNEL_RWX) \
    $(wildcard include/config/STRICT_MODULE_RWX) \
    $(wildcard include/config/LTO_CLANG) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/atomic_lse.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/atomic/atomic-arch-fallback.h \
    $(wildcard include/config/GENERIC_ATOMIC64) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/atomic/atomic-long.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/atomic/atomic-instrumented.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/instrumented.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/instrumented-atomic.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/lock.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/instrumented-lock.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/non-atomic.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/le.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/byteorder.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/byteorder/little_endian.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/byteorder/little_endian.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/swab.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/swab.h \
  arch/arm64/include/generated/uapi/asm/swab.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/swab.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/byteorder/generic.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bitops/ext2-atomic-setbit.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kstrtox.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/log2.h \
    $(wildcard include/config/ARCH_HAS_ILOG2_U32) \
    $(wildcard include/config/ARCH_HAS_ILOG2_U64) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/math.h \
  arch/arm64/include/generated/asm/div64.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/div64.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/minmax.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/panic.h \
    $(wildcard include/config/PANIC_TIMEOUT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/printk.h \
    $(wildcard include/config/MESSAGE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_QUIET) \
    $(wildcard include/config/EARLY_PRINTK) \
    $(wildcard include/config/PRINTK) \
    $(wildcard include/config/DYNAMIC_DEBUG) \
    $(wildcard include/config/DYNAMIC_DEBUG_CORE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kern_levels.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/ratelimit_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/param.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/param.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/param.h \
    $(wildcard include/config/HZ) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/param.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/spinlock_types.h \
    $(wildcard include/config/PREEMPT_RT) \
    $(wildcard include/config/DEBUG_LOCK_ALLOC) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/spinlock_types_raw.h \
    $(wildcard include/config/DEBUG_SPINLOCK) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/spinlock_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/qspinlock_types.h \
    $(wildcard include/config/NR_CPUS) \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/qrwlock_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/lockdep_types.h \
    $(wildcard include/config/PROVE_RAW_LOCK_NESTING) \
    $(wildcard include/config/PREEMPT_LOCK) \
    $(wildcard include/config/LOCKDEP) \
    $(wildcard include/config/LOCK_STAT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rwlock_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/once_lite.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/static_call_types.h \
    $(wildcard include/config/HAVE_STATIC_CALL) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/stat.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/stat.h \
    $(wildcard include/config/COMPAT) \
  arch/arm64/include/generated/uapi/asm/stat.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/stat.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/time.h \
    $(wildcard include/config/POSIX_TIMERS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/cache.h \
    $(wildcard include/config/ARCH_HAS_CACHE_LINE_SIZE) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/cache.h \
    $(wildcard include/config/ARM64_KMALLOC64) \
    $(wildcard include/config/KASAN_HW_TAGS) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/cputype.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/sysreg.h \
    $(wildcard include/config/BROKEN_GAS_INST) \
    $(wildcard include/config/ARM64_PA_BITS_52) \
    $(wildcard include/config/ARM64_4K_PAGES) \
    $(wildcard include/config/ARM64_16K_PAGES) \
    $(wildcard include/config/ARM64_64K_PAGES) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kasan-tags.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/mte-def.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kasan-enabled.h \
    $(wildcard include/config/KASAN) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/static_key.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/math64.h \
    $(wildcard include/config/ARCH_SUPPORTS_INT128) \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/math64.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/time64.h \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/time64.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/time.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/time_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/time32.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/timex.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/timex.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/timex.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/arch_timer.h \
    $(wildcard include/config/ARM_ARCH_TIMER_OOL_WORKAROUND) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/hwcap.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/hwcap.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/cpufeature.h \
    $(wildcard include/config/ARM64_PAN) \
    $(wildcard include/config/ARM64_SW_TTBR0_PAN) \
    $(wildcard include/config/ARM64_SVE) \
    $(wildcard include/config/ARM64_CNP) \
    $(wildcard include/config/ARM64_PTR_AUTH) \
    $(wildcard include/config/ARM64_MTE) \
    $(wildcard include/config/ARM64_DEBUG_PRIORITY_MASKING) \
    $(wildcard include/config/ARM64_BTI) \
    $(wildcard include/config/ARM64_TLB_RANGE) \
    $(wildcard include/config/ARM64_PA_BITS) \
    $(wildcard include/config/ARM64_HW_AFDBM) \
    $(wildcard include/config/ARM64_AMU_EXTN) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/bug.h \
    $(wildcard include/config/BUG_ON_DATA_CORRUPTION) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/bug.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/asm-bug.h \
    $(wildcard include/config/DEBUG_BUGVERBOSE) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/brk-imm.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/bug.h \
    $(wildcard include/config/BUG) \
    $(wildcard include/config/GENERIC_BUG_RELATIVE_POINTERS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/instrumentation.h \
    $(wildcard include/config/DEBUG_ENTRY) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/smp.h \
    $(wildcard include/config/UP_LATE_INIT) \
    $(wildcard include/config/DEBUG_PREEMPT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/errno.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/errno.h \
  arch/arm64/include/generated/uapi/asm/errno.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/errno.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/errno-base.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/cpumask.h \
    $(wildcard include/config/CPUMASK_OFFSTACK) \
    $(wildcard include/config/HOTPLUG_CPU) \
    $(wildcard include/config/DEBUG_PER_CPU_MAPS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/threads.h \
    $(wildcard include/config/BASE_SMALL) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/bitmap.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/string.h \
    $(wildcard include/config/BINARY_PRINTF) \
    $(wildcard include/config/FORTIFY_SOURCE) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/string.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/string.h \
    $(wildcard include/config/ARCH_HAS_UACCESS_FLUSHCACHE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/smp_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/llist.h \
    $(wildcard include/config/ARCH_HAVE_NMI_SAFE_CMPXCHG) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/preempt.h \
    $(wildcard include/config/PREEMPT_COUNT) \
    $(wildcard include/config/TRACE_PREEMPT_TOGGLE) \
    $(wildcard include/config/PREEMPTION) \
    $(wildcard include/config/PREEMPT_NOTIFIERS) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/preempt.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/thread_info.h \
    $(wildcard include/config/THREAD_INFO_IN_TASK) \
    $(wildcard include/config/GENERIC_ENTRY) \
    $(wildcard include/config/HAVE_ARCH_WITHIN_STACK_FRAMES) \
    $(wildcard include/config/HARDENED_USERCOPY) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/restart_block.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/current.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/thread_info.h \
    $(wildcard include/config/SHADOW_CALL_STACK) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/memory.h \
    $(wildcard include/config/ARM64_VA_BITS) \
    $(wildcard include/config/KASAN_SHADOW_OFFSET) \
    $(wildcard include/config/VMAP_STACK) \
    $(wildcard include/config/DEBUG_VIRTUAL) \
    $(wildcard include/config/EFI) \
    $(wildcard include/config/ARM_GIC_V3_ITS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sizes.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/page-def.h \
    $(wildcard include/config/ARM64_PAGE_SHIFT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mmdebug.h \
    $(wildcard include/config/DEBUG_VM) \
    $(wildcard include/config/DEBUG_VM_PGFLAGS) \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/memory_model.h \
    $(wildcard include/config/FLATMEM) \
    $(wildcard include/config/SPARSEMEM_VMEMMAP) \
    $(wildcard include/config/SPARSEMEM) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/pfn.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/stack_pointer.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/smp.h \
    $(wildcard include/config/ARM64_ACPI_PARKING_PROTOCOL) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/percpu.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/percpu.h \
    $(wildcard include/config/HAVE_SETUP_PER_CPU_AREA) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/percpu-defs.h \
    $(wildcard include/config/DEBUG_FORCE_WEAK_PER_CPU) \
    $(wildcard include/config/AMD_MEM_ENCRYPT) \
  /home/zhi/derp/kernel/meizu/m2391/include/clocksource/arm_arch_timer.h \
    $(wildcard include/config/ARM_ARCH_TIMER) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/timecounter.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/timex.h \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/time32.h \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/time.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/compat.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/compat.h \
    $(wildcard include/config/COMPAT_FOR_U64_ALIGNMENT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched.h \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING_NATIVE) \
    $(wildcard include/config/SCHED_INFO) \
    $(wildcard include/config/SCHEDSTATS) \
    $(wildcard include/config/FAIR_GROUP_SCHED) \
    $(wildcard include/config/RT_GROUP_SCHED) \
    $(wildcard include/config/RT_MUTEXES) \
    $(wildcard include/config/UCLAMP_TASK) \
    $(wildcard include/config/UCLAMP_BUCKETS_COUNT) \
    $(wildcard include/config/KMAP_LOCAL) \
    $(wildcard include/config/SCHED_CORE) \
    $(wildcard include/config/CGROUP_SCHED) \
    $(wildcard include/config/BLK_DEV_IO_TRACE) \
    $(wildcard include/config/PREEMPT_RCU) \
    $(wildcard include/config/TASKS_RCU) \
    $(wildcard include/config/TASKS_TRACE_RCU) \
    $(wildcard include/config/PSI) \
    $(wildcard include/config/MEMCG) \
    $(wildcard include/config/LRU_GEN) \
    $(wildcard include/config/COMPAT_BRK) \
    $(wildcard include/config/CGROUPS) \
    $(wildcard include/config/BLK_CGROUP) \
    $(wildcard include/config/PAGE_OWNER) \
    $(wildcard include/config/EVENTFD) \
    $(wildcard include/config/STACKPROTECTOR) \
    $(wildcard include/config/ARCH_HAS_SCALED_CPUTIME) \
    $(wildcard include/config/CPU_FREQ_TIMES) \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING_GEN) \
    $(wildcard include/config/NO_HZ_FULL) \
    $(wildcard include/config/POSIX_CPUTIMERS) \
    $(wildcard include/config/POSIX_CPU_TIMERS_TASK_WORK) \
    $(wildcard include/config/KEYS) \
    $(wildcard include/config/SYSVIPC) \
    $(wildcard include/config/DETECT_HUNG_TASK) \
    $(wildcard include/config/IO_URING) \
    $(wildcard include/config/AUDIT) \
    $(wildcard include/config/AUDITSYSCALL) \
    $(wildcard include/config/DEBUG_MUTEXES) \
    $(wildcard include/config/TRACE_IRQFLAGS) \
    $(wildcard include/config/UBSAN) \
    $(wildcard include/config/UBSAN_TRAP) \
    $(wildcard include/config/BLOCK) \
    $(wildcard include/config/COMPACTION) \
    $(wildcard include/config/TASK_XACCT) \
    $(wildcard include/config/CPUSETS) \
    $(wildcard include/config/X86_CPU_RESCTRL) \
    $(wildcard include/config/FUTEX) \
    $(wildcard include/config/PERF_EVENTS) \
    $(wildcard include/config/NUMA) \
    $(wildcard include/config/NUMA_BALANCING) \
    $(wildcard include/config/RSEQ) \
    $(wildcard include/config/TASK_DELAY_ACCT) \
    $(wildcard include/config/FAULT_INJECTION) \
    $(wildcard include/config/LATENCYTOP) \
    $(wildcard include/config/FUNCTION_GRAPH_TRACER) \
    $(wildcard include/config/KCOV) \
    $(wildcard include/config/UPROBES) \
    $(wildcard include/config/BCACHE) \
    $(wildcard include/config/SECURITY) \
    $(wildcard include/config/BPF_SYSCALL) \
    $(wildcard include/config/GCC_PLUGIN_STACKLEAK) \
    $(wildcard include/config/X86_MCE) \
    $(wildcard include/config/KRETPROBES) \
    $(wildcard include/config/ARCH_HAS_PARANOID_L1D_FLUSH) \
    $(wildcard include/config/RT_SOFTINT_OPTIMIZATION) \
    $(wildcard include/config/ARCH_TASK_STRUCT_ON_STACK) \
    $(wildcard include/config/DEBUG_RSEQ) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/sched.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/pid.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rculist.h \
    $(wildcard include/config/PROVE_RCU_LIST) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rcupdate.h \
    $(wildcard include/config/TINY_RCU) \
    $(wildcard include/config/TASKS_RCU_GENERIC) \
    $(wildcard include/config/RCU_STALL_COMMON) \
    $(wildcard include/config/RCU_NOCB_CPU) \
    $(wildcard include/config/TASKS_RUDE_RCU) \
    $(wildcard include/config/TREE_RCU) \
    $(wildcard include/config/DEBUG_OBJECTS_RCU_HEAD) \
    $(wildcard include/config/PROVE_RCU) \
    $(wildcard include/config/ARCH_WEAK_RELEASE_ACQUIRE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/irqflags.h \
    $(wildcard include/config/IRQSOFF_TRACER) \
    $(wildcard include/config/PREEMPT_TRACER) \
    $(wildcard include/config/DEBUG_IRQFLAGS) \
    $(wildcard include/config/TRACE_IRQFLAGS_SUPPORT) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/irqflags.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/ptrace.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/ptrace.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/sve_context.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/bottom_half.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/lockdep.h \
    $(wildcard include/config/DEBUG_LOCKING_API_SELFTESTS) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/processor.h \
    $(wildcard include/config/KUSER_HELPERS) \
    $(wildcard include/config/ARM64_FORCE_52BIT) \
    $(wildcard include/config/HAVE_HW_BREAKPOINT) \
    $(wildcard include/config/ARM64_PTR_AUTH_KERNEL) \
    $(wildcard include/config/ARM64_TAGGED_ADDR_ABI) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/android_vendor.h \
    $(wildcard include/config/ANDROID_VENDOR_OEM_DATA) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/android_kabi.h \
    $(wildcard include/config/ANDROID_KABI_RESERVE) \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/processor.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/vdso/processor.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/hw_breakpoint.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/virt.h \
    $(wildcard include/config/KVM) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/sections.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/sections.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/kasan.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/mte-kasan.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/pgtable-types.h \
    $(wildcard include/config/PGTABLE_LEVELS) \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/pgtable-nopud.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/pgtable-nop4d.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/pgtable-hwdef.h \
    $(wildcard include/config/ARM64_CONT_PTE_SHIFT) \
    $(wildcard include/config/ARM64_CONT_PMD_SHIFT) \
    $(wildcard include/config/ARM64_VA_BITS_52) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/pointer_auth.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/prctl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/random.h \
    $(wildcard include/config/ARCH_RANDOM) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/once.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/random.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/ioctl.h \
  arch/arm64/include/generated/uapi/asm/ioctl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/ioctl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/ioctl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/irqnr.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/irqnr.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/prandom.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/percpu.h \
    $(wildcard include/config/NEED_PER_CPU_EMBED_FIRST_CHUNK) \
    $(wildcard include/config/NEED_PER_CPU_PAGE_FIRST_CHUNK) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/siphash.h \
    $(wildcard include/config/HAVE_EFFICIENT_UNALIGNED_ACCESS) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/archrandom.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/arm-smccc.h \
    $(wildcard include/config/ARM64) \
    $(wildcard include/config/HAVE_ARM_SMCCC) \
    $(wildcard include/config/ARM) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/spectre.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/fpsimd.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/sigcontext.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rcutree.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/wait.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/spinlock.h \
  arch/arm64/include/generated/asm/mmiowb.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/mmiowb.h \
    $(wildcard include/config/MMIOWB) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/spinlock.h \
  arch/arm64/include/generated/asm/qspinlock.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/qspinlock.h \
  arch/arm64/include/generated/asm/qrwlock.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/qrwlock.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rwlock.h \
    $(wildcard include/config/PREEMPT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/spinlock_api_smp.h \
    $(wildcard include/config/INLINE_SPIN_LOCK) \
    $(wildcard include/config/INLINE_SPIN_LOCK_BH) \
    $(wildcard include/config/INLINE_SPIN_LOCK_IRQ) \
    $(wildcard include/config/INLINE_SPIN_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_SPIN_TRYLOCK) \
    $(wildcard include/config/INLINE_SPIN_TRYLOCK_BH) \
    $(wildcard include/config/UNINLINE_SPIN_UNLOCK) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_BH) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_IRQRESTORE) \
    $(wildcard include/config/GENERIC_LOCKBREAK) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rwlock_api_smp.h \
    $(wildcard include/config/INLINE_READ_LOCK) \
    $(wildcard include/config/INLINE_WRITE_LOCK) \
    $(wildcard include/config/INLINE_READ_LOCK_BH) \
    $(wildcard include/config/INLINE_WRITE_LOCK_BH) \
    $(wildcard include/config/INLINE_READ_LOCK_IRQ) \
    $(wildcard include/config/INLINE_WRITE_LOCK_IRQ) \
    $(wildcard include/config/INLINE_READ_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_WRITE_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_READ_TRYLOCK) \
    $(wildcard include/config/INLINE_WRITE_TRYLOCK) \
    $(wildcard include/config/INLINE_READ_UNLOCK) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK) \
    $(wildcard include/config/INLINE_READ_UNLOCK_BH) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_BH) \
    $(wildcard include/config/INLINE_READ_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_READ_UNLOCK_IRQRESTORE) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_IRQRESTORE) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/wait.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/refcount.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sem.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/sem.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/ipc.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/uidgid.h \
    $(wildcard include/config/MULTIUSER) \
    $(wildcard include/config/USER_NS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/highuid.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rhashtable-types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mutex.h \
    $(wildcard include/config/MUTEX_SPIN_ON_OWNER) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/osq_lock.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/debug_locks.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/workqueue.h \
    $(wildcard include/config/DEBUG_OBJECTS_WORK) \
    $(wildcard include/config/FREEZER) \
    $(wildcard include/config/WQ_WATCHDOG) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/timer.h \
    $(wildcard include/config/DEBUG_OBJECTS_TIMERS) \
    $(wildcard include/config/NO_HZ_COMMON) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/ktime.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/jiffies.h \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/jiffies.h \
  include/generated/timeconst.h \
  /home/zhi/derp/kernel/meizu/m2391/include/vdso/ktime.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/timekeeping.h \
    $(wildcard include/config/GENERIC_CMOS_UPDATE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/clocksource_ids.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/debugobjects.h \
    $(wildcard include/config/DEBUG_OBJECTS) \
    $(wildcard include/config/DEBUG_OBJECTS_FREE) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/ipc.h \
  arch/arm64/include/generated/uapi/asm/ipcbuf.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/ipcbuf.h \
  arch/arm64/include/generated/uapi/asm/sembuf.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/sembuf.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/shm.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/page.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/personality.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/personality.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/getorder.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/shm.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/hugetlb_encode.h \
  arch/arm64/include/generated/uapi/asm/shmbuf.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/shmbuf.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/shmparam.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/shmparam.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/plist.h \
    $(wildcard include/config/DEBUG_PLIST) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/hrtimer.h \
    $(wildcard include/config/HIGH_RES_TIMERS) \
    $(wildcard include/config/TIME_LOW_RES) \
    $(wildcard include/config/TIMERFD) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/hrtimer_defs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rbtree.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rbtree_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/seqlock.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/ww_mutex.h \
    $(wildcard include/config/DEBUG_RT_MUTEXES) \
    $(wildcard include/config/DEBUG_WW_MUTEX_SLOWPATH) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rtmutex.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/timerqueue.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/seccomp.h \
    $(wildcard include/config/SECCOMP) \
    $(wildcard include/config/HAVE_ARCH_SECCOMP_FILTER) \
    $(wildcard include/config/SECCOMP_FILTER) \
    $(wildcard include/config/CHECKPOINT_RESTORE) \
    $(wildcard include/config/SECCOMP_CACHE_DEBUG) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/seccomp.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/seccomp.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/unistd.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/unistd.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/unistd.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/seccomp.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/unistd.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/nodemask.h \
    $(wildcard include/config/HIGHMEM) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/numa.h \
    $(wildcard include/config/NODES_SHIFT) \
    $(wildcard include/config/NUMA_KEEP_MEMINFO) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/resource.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/resource.h \
  arch/arm64/include/generated/uapi/asm/resource.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/resource.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/resource.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/latencytop.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/prio.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/signal_types.h \
    $(wildcard include/config/OLD_SIGACTION) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/signal.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/signal.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/signal.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/signal.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/signal.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/signal-defs.h \
  arch/arm64/include/generated/uapi/asm/siginfo.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/siginfo.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/syscall_user_dispatch.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mm_types_task.h \
    $(wildcard include/config/ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH) \
    $(wildcard include/config/SPLIT_PTLOCK_CPUS) \
    $(wildcard include/config/ARCH_ENABLE_SPLIT_PMD_PTLOCK) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/task_io_accounting.h \
    $(wildcard include/config/TASK_IO_ACCOUNTING) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/posix-timers.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/alarmtimer.h \
    $(wildcard include/config/RTC_CLASS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/task_work.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/rseq.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kcsan.h \
  arch/arm64/include/generated/asm/kmap_size.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/kmap_size.h \
    $(wildcard include/config/DEBUG_KMAP_LOCAL) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/task_stack.h \
    $(wildcard include/config/STACK_GROWSUP) \
    $(wildcard include/config/DEBUG_STACK_USAGE) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/magic.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/stat.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/buildid.h \
    $(wildcard include/config/CRASH_CORE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mm_types.h \
    $(wildcard include/config/HAVE_ALIGNED_STRUCT_PAGE) \
    $(wildcard include/config/USERFAULTFD) \
    $(wildcard include/config/SPECULATIVE_PAGE_FAULT) \
    $(wildcard include/config/SWAP) \
    $(wildcard include/config/HAVE_ARCH_COMPAT_MMAP_BASES) \
    $(wildcard include/config/MEMBARRIER) \
    $(wildcard include/config/AIO) \
    $(wildcard include/config/MMU_NOTIFIER) \
    $(wildcard include/config/TRANSPARENT_HUGEPAGE) \
    $(wildcard include/config/HUGETLB_PAGE) \
    $(wildcard include/config/IOMMU_SUPPORT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/auxvec.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/auxvec.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/auxvec.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kref.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rwsem.h \
    $(wildcard include/config/RWSEM_SPIN_ON_OWNER) \
    $(wildcard include/config/DEBUG_RWSEMS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/err.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/completion.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/swait.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/uprobes.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/uprobes.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/debug-monitors.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/esr.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/probes.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/page-flags-layout.h \
  include/generated/bounds.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/sparsemem.h \
    $(wildcard include/config/ARM64_MEMMAP_ON_MEMORY) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/mmu.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kmod.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/umh.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/gfp.h \
    $(wildcard include/config/CMA) \
    $(wildcard include/config/ZONE_DMA) \
    $(wildcard include/config/ZONE_DMA32) \
    $(wildcard include/config/ZONE_DEVICE) \
    $(wildcard include/config/PM_SLEEP) \
    $(wildcard include/config/CONTIG_ALLOC) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mmzone.h \
    $(wildcard include/config/FORCE_MAX_ZONEORDER) \
    $(wildcard include/config/MEMORY_ISOLATION) \
    $(wildcard include/config/LRU_GEN_STATS) \
    $(wildcard include/config/MEMORY_HOTPLUG) \
    $(wildcard include/config/PAGE_EXTENSION) \
    $(wildcard include/config/DEFERRED_STRUCT_PAGE_INIT) \
    $(wildcard include/config/HAVE_MEMORYLESS_NODES) \
    $(wildcard include/config/SPARSEMEM_EXTREME) \
    $(wildcard include/config/HAVE_ARCH_PFN_VALID) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/pageblock-flags.h \
    $(wildcard include/config/HUGETLB_PAGE_SIZE_VARIABLE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/page-flags.h \
    $(wildcard include/config/ARCH_USES_PG_UNCACHED) \
    $(wildcard include/config/MEMORY_FAILURE) \
    $(wildcard include/config/PAGE_IDLE_FLAG) \
    $(wildcard include/config/THP_SWAP) \
    $(wildcard include/config/KSM) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/local_lock.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/local_lock_internal.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/memory_hotplug.h \
    $(wildcard include/config/ARCH_HAS_ADD_PAGES) \
    $(wildcard include/config/HAVE_ARCH_NODEDATA_EXTENSION) \
    $(wildcard include/config/MEMORY_HOTREMOVE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/notifier.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/srcu.h \
    $(wildcard include/config/TINY_SRCU) \
    $(wildcard include/config/SRCU) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rcu_segcblist.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/srcutree.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rcu_node_tree.h \
    $(wildcard include/config/RCU_FANOUT) \
    $(wildcard include/config/RCU_FANOUT_LEAF) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/topology.h \
    $(wildcard include/config/USE_PERCPU_NUMA_NODE_ID) \
    $(wildcard include/config/SCHED_SMT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/arch_topology.h \
    $(wildcard include/config/GENERIC_ARCH_TOPOLOGY) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/topology.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/topology.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sysctl.h \
    $(wildcard include/config/SYSCTL) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/sysctl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/elf.h \
    $(wildcard include/config/ARCH_USE_GNU_PROPERTY) \
    $(wildcard include/config/ARCH_HAVE_ELF_PROT) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/elf.h \
    $(wildcard include/config/COMPAT_VDSO) \
  arch/arm64/include/generated/asm/user.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/user.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/elf.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/elf-em.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/fs.h \
    $(wildcard include/config/READ_ONLY_THP_FOR_FS) \
    $(wildcard include/config/FS_POSIX_ACL) \
    $(wildcard include/config/CGROUP_WRITEBACK) \
    $(wildcard include/config/IMA) \
    $(wildcard include/config/FILE_LOCKING) \
    $(wildcard include/config/FSNOTIFY) \
    $(wildcard include/config/FS_ENCRYPTION) \
    $(wildcard include/config/FS_VERITY) \
    $(wildcard include/config/EPOLL) \
    $(wildcard include/config/UNICODE) \
    $(wildcard include/config/QUOTA) \
    $(wildcard include/config/FS_DAX) \
    $(wildcard include/config/MIGRATION) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/wait_bit.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kdev_t.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/kdev_t.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/dcache.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rculist_bl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/list_bl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/bit_spinlock.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/lockref.h \
    $(wildcard include/config/ARCH_USE_CMPXCHG_LOCKREF) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/stringhash.h \
    $(wildcard include/config/DCACHE_WORD_ACCESS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/hash.h \
    $(wildcard include/config/HAVE_ARCH_HASH) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/path.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/list_lru.h \
    $(wildcard include/config/MEMCG_KMEM) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/shrinker.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/radix-tree.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/xarray.h \
    $(wildcard include/config/XARRAY_MULTI) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/capability.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/capability.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/semaphore.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/fcntl.h \
    $(wildcard include/config/ARCH_32BIT_OFF_T) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/fcntl.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/uapi/asm/fcntl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/fcntl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/openat2.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/migrate_mode.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/percpu-rwsem.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rcuwait.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/signal.h \
    $(wildcard include/config/SCHED_AUTOGROUP) \
    $(wildcard include/config/BSD_PROCESS_ACCT) \
    $(wildcard include/config/TASKSTATS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/signal.h \
    $(wildcard include/config/PROC_FS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/jobctl.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/task.h \
    $(wildcard include/config/HAVE_EXIT_THREAD) \
    $(wildcard include/config/ARCH_WANTS_DYNAMIC_TASK_STRUCT) \
    $(wildcard include/config/HAVE_ARCH_THREAD_STRUCT_WHITELIST) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/uaccess.h \
    $(wildcard include/config/SET_FS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/fault-inject-usercopy.h \
    $(wildcard include/config/FAULT_INJECTION_USERCOPY) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/uaccess.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/kernel-pgtable.h \
    $(wildcard include/config/RANDOMIZE_BASE) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/mte.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/bitfield.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/extable.h \
    $(wildcard include/config/BPF_JIT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/cred.h \
    $(wildcard include/config/DEBUG_CREDENTIALS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/key.h \
    $(wildcard include/config/KEY_NOTIFICATIONS) \
    $(wildcard include/config/NET) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/assoc_array.h \
    $(wildcard include/config/ASSOCIATIVE_ARRAY) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/user.h \
    $(wildcard include/config/WATCH_QUEUE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/percpu_counter.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/ratelimit.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rcu_sync.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/delayed_call.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/uuid.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/uuid.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/errseq.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/ioprio.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/rt.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/iocontext.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/ioprio.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/fs_types.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mount.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mnt_idmapping.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/fs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/quota.h \
    $(wildcard include/config/QUOTA_NETLINK_INTERFACE) \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/dqblk_xfs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/dqblk_v1.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/dqblk_v2.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/dqblk_qtree.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/projid.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/quota.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/nfs_fs_i.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kobject.h \
    $(wildcard include/config/UEVENT_HELPER) \
    $(wildcard include/config/DEBUG_KOBJECT_RELEASE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sysfs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kernfs.h \
    $(wildcard include/config/KERNFS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/idr.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kobject_ns.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/moduleparam.h \
    $(wildcard include/config/ALPHA) \
    $(wildcard include/config/IA64) \
    $(wildcard include/config/PPC64) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/rbtree_latch.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/error-injection.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/error-injection.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/tracepoint-defs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/cfi.h \
    $(wildcard include/config/CFI_CLANG_SHADOW) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/module.h \
    $(wildcard include/config/ARM64_MODULE_PLTS) \
    $(wildcard include/config/DYNAMIC_FTRACE) \
    $(wildcard include/config/ARM64_ERRATUM_843419) \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/module.h \
    $(wildcard include/config/HAVE_MOD_ARCH_SPECIFIC) \
    $(wildcard include/config/MODULES_USE_ELF_REL) \
    $(wildcard include/config/MODULES_USE_ELF_RELA) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/of.h \
    $(wildcard include/config/OF_DYNAMIC) \
    $(wildcard include/config/SPARC) \
    $(wildcard include/config/OF_PROMTREE) \
    $(wildcard include/config/OF_KOBJ) \
    $(wildcard include/config/OF) \
    $(wildcard include/config/OF_NUMA) \
    $(wildcard include/config/OF_OVERLAY) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mod_devicetable.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/property.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/fwnode.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/platform_device.h \
    $(wildcard include/config/SUSPEND) \
    $(wildcard include/config/HIBERNATE_CALLBACKS) \
    $(wildcard include/config/SUPERH) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/device.h \
    $(wildcard include/config/ENERGY_MODEL) \
    $(wildcard include/config/GENERIC_MSI_IRQ_DOMAIN) \
    $(wildcard include/config/PINCTRL) \
    $(wildcard include/config/GENERIC_MSI_IRQ) \
    $(wildcard include/config/DMA_OPS) \
    $(wildcard include/config/DMA_DECLARE_COHERENT) \
    $(wildcard include/config/DMA_CMA) \
    $(wildcard include/config/SWIOTLB) \
    $(wildcard include/config/ARCH_HAS_SYNC_DMA_FOR_DEVICE) \
    $(wildcard include/config/ARCH_HAS_SYNC_DMA_FOR_CPU) \
    $(wildcard include/config/ARCH_HAS_SYNC_DMA_FOR_CPU_ALL) \
    $(wildcard include/config/DMA_OPS_BYPASS) \
    $(wildcard include/config/DEVTMPFS) \
    $(wildcard include/config/SYSFS_DEPRECATED) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/dev_printk.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/energy_model.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/cpufreq.h \
    $(wildcard include/config/CPU_FREQ) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/topology.h \
    $(wildcard include/config/SCHED_DEBUG) \
    $(wildcard include/config/SCHED_MC) \
    $(wildcard include/config/CPU_FREQ_GOV_SCHEDUTIL) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/idle.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/sd_flags.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/ioport.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/klist.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/pm.h \
    $(wildcard include/config/VT_CONSOLE_SLEEP) \
    $(wildcard include/config/PM) \
    $(wildcard include/config/PM_CLK) \
    $(wildcard include/config/PM_GENERIC_DOMAINS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/overflow.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/device/bus.h \
    $(wildcard include/config/ACPI) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/device/class.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/device/driver.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/device.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/pm_wakeup.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/soc.h \
    $(wildcard include/config/SND_SOC_COMPRESS) \
    $(wildcard include/config/DMI) \
    $(wildcard include/config/SND_SOC_AC97_BUS) \
    $(wildcard include/config/SND_SOC_TOPOLOGY) \
    $(wildcard include/config/DEBUG_FS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/interrupt.h \
    $(wildcard include/config/IRQ_FORCED_THREADING) \
    $(wildcard include/config/GENERIC_IRQ_PROBE) \
    $(wildcard include/config/IRQ_TIMINGS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/irqreturn.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/hardirq.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/context_tracking_state.h \
    $(wildcard include/config/CONTEXT_TRACKING) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/ftrace_irq.h \
    $(wildcard include/config/HWLAT_TRACER) \
    $(wildcard include/config/OSNOISE_TRACER) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/vtime.h \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING) \
    $(wildcard include/config/IRQ_TIME_ACCOUNTING) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/hardirq.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/irq.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/irq.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/kvm_arm.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/hardirq.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/irq.h \
    $(wildcard include/config/GENERIC_IRQ_EFFECTIVE_AFF_MASK) \
    $(wildcard include/config/GENERIC_IRQ_IPI) \
    $(wildcard include/config/IRQ_DOMAIN_HIERARCHY) \
    $(wildcard include/config/GENERIC_IRQ_MIGRATION) \
    $(wildcard include/config/GENERIC_PENDING_IRQ) \
    $(wildcard include/config/HARDIRQS_SW_RESEND) \
    $(wildcard include/config/GENERIC_IRQ_LEGACY) \
    $(wildcard include/config/GENERIC_IRQ_MULTI_HANDLER) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/irqhandler.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/io.h \
    $(wildcard include/config/HAS_IOPORT_MAP) \
    $(wildcard include/config/PCI) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/io.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/pgtable.h \
    $(wildcard include/config/HIGHPTE) \
    $(wildcard include/config/ARCH_HAS_NONLEAF_PMD_YOUNG) \
    $(wildcard include/config/GUP_GET_PTE_LOW_HIGH) \
    $(wildcard include/config/HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD) \
    $(wildcard include/config/HAVE_ARCH_SOFT_DIRTY) \
    $(wildcard include/config/ARCH_ENABLE_THP_MIGRATION) \
    $(wildcard include/config/ARCH_HAS_PTE_DEVMAP) \
    $(wildcard include/config/HAVE_ARCH_HUGE_VMAP) \
    $(wildcard include/config/X86_ESPFIX64) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/pgtable.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/proc-fns.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/pgtable-prot.h \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/tlbflush.h \
    $(wildcard include/config/ARM64_WORKAROUND_REPEAT_TLBI) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/fixmap.h \
    $(wildcard include/config/ACPI_APEI_GHES) \
    $(wildcard include/config/ARM_SDE_INTERFACE) \
    $(wildcard include/config/UNMAP_KERNEL_AT_EL0) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/boot.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/fixmap.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/pgtable_uffd.h \
    $(wildcard include/config/HAVE_ARCH_USERFAULTFD_WP) \
  arch/arm64/include/generated/asm/early_ioremap.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/early_ioremap.h \
    $(wildcard include/config/GENERIC_EARLY_IOREMAP) \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/io.h \
    $(wildcard include/config/GENERIC_IOMAP) \
    $(wildcard include/config/TRACE_MMIO_ACCESS) \
    $(wildcard include/config/GENERIC_IOREMAP) \
    $(wildcard include/config/VIRT_TO_BUS) \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/pci_iomap.h \
    $(wildcard include/config/NO_GENERIC_PCI_IOPORT_MAP) \
    $(wildcard include/config/GENERIC_PCI_IOMAP) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/logic_pio.h \
    $(wildcard include/config/INDIRECT_PIO) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/vmalloc.h \
    $(wildcard include/config/KASAN_VMALLOC) \
    $(wildcard include/config/HAVE_ARCH_HUGE_VMALLOC) \
  /home/zhi/derp/kernel/meizu/m2391/arch/arm64/include/asm/vmalloc.h \
    $(wildcard include/config/PTDUMP_DEBUGFS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/slab.h \
    $(wildcard include/config/DEBUG_SLAB) \
    $(wildcard include/config/FAILSLAB) \
    $(wildcard include/config/HAVE_HARDENED_USERCOPY_ALLOCATOR) \
    $(wildcard include/config/SLAB) \
    $(wildcard include/config/SLUB) \
    $(wildcard include/config/SLOB) \
    $(wildcard include/config/CC_IS_GCC) \
    $(wildcard include/config/CLANG_VERSION) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/percpu-refcount.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/kasan.h \
    $(wildcard include/config/KASAN_STACK) \
    $(wildcard include/config/KASAN_INLINE) \
  arch/arm64/include/generated/asm/irq_regs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/irq_regs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/irqdesc.h \
    $(wildcard include/config/GENERIC_IRQ_DEBUGFS) \
    $(wildcard include/config/SPARSE_IRQ) \
    $(wildcard include/config/IRQ_DOMAIN) \
    $(wildcard include/config/HANDLE_DOMAIN_IRQ) \
  arch/arm64/include/generated/asm/hw_irq.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/hw_irq.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/regmap.h \
    $(wildcard include/config/REGMAP) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/delay.h \
  arch/arm64/include/generated/asm/delay.h \
  /home/zhi/derp/kernel/meizu/m2391/include/asm-generic/delay.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/iopoll.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/core.h \
    $(wildcard include/config/SND_DYNAMIC_MINORS) \
    $(wildcard include/config/SND_MAX_CARDS) \
    $(wildcard include/config/SND_MAJOR) \
    $(wildcard include/config/SND_CTL_FAST_LOOKUP) \
    $(wildcard include/config/SND_DEBUG) \
    $(wildcard include/config/SND_MIXER_OSS) \
    $(wildcard include/config/SND_OSSEMUL) \
    $(wildcard include/config/ISA_DMA_API) \
    $(wildcard include/config/SND_VERBOSE_PRINTK) \
    $(wildcard include/config/SND_DEBUG_VERBOSE) \
    $(wildcard include/config/GAMEPORT) \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/pcm.h \
    $(wildcard include/config/SND_PCM_OSS) \
    $(wildcard include/config/SND_VERBOSE_PROCFS) \
    $(wildcard include/config/SND_PCM_XRUN_DEBUG) \
    $(wildcard include/config/X86) \
    $(wildcard include/config/PPC) \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/asound.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/sound/asound.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/memalloc.h \
    $(wildcard include/config/SND_DMA_SGBUF) \
    $(wildcard include/config/GENERIC_ALLOCATOR) \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/minors.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/poll.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/poll.h \
  arch/arm64/include/generated/uapi/asm/poll.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/asm-generic/poll.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/linux/eventpoll.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mm.h \
    $(wildcard include/config/HAVE_ARCH_MMAP_RND_BITS) \
    $(wildcard include/config/HAVE_ARCH_MMAP_RND_COMPAT_BITS) \
    $(wildcard include/config/DEBUG_INFO_BTF) \
    $(wildcard include/config/MEM_SOFT_DIRTY) \
    $(wildcard include/config/ARCH_USES_HIGH_VMA_FLAGS) \
    $(wildcard include/config/ARCH_HAS_PKEYS) \
    $(wildcard include/config/PARISC) \
    $(wildcard include/config/SPARC64) \
    $(wildcard include/config/HAVE_ARCH_USERFAULTFD_MINOR) \
    $(wildcard include/config/SHMEM) \
    $(wildcard include/config/DEV_PAGEMAP_OPS) \
    $(wildcard include/config/DEVICE_PRIVATE) \
    $(wildcard include/config/PCI_P2PDMA) \
    $(wildcard include/config/ARCH_HAS_PTE_SPECIAL) \
    $(wildcard include/config/DEBUG_VM_RB) \
    $(wildcard include/config/PAGE_POISONING) \
    $(wildcard include/config/INIT_ON_ALLOC_DEFAULT_ON) \
    $(wildcard include/config/INIT_ON_FREE_DEFAULT_ON) \
    $(wildcard include/config/DEBUG_PAGEALLOC) \
    $(wildcard include/config/HUGETLBFS) \
    $(wildcard include/config/MAPPING_DIRTY_HELPERS) \
    $(wildcard include/config/ANON_VMA_NAME) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/mmap_lock.h \
    $(wildcard include/config/SPECULATIVE_PAGE_FAULT_STATS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/vm_event.h \
    $(wildcard include/config/VM_EVENT_COUNTERS) \
    $(wildcard include/config/DEBUG_TLBFLUSH) \
    $(wildcard include/config/DEBUG_VM_VMACACHE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/vm_event_item.h \
    $(wildcard include/config/MEMORY_BALLOON) \
    $(wildcard include/config/BALLOON_COMPACTION) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/range.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/page_ext.h \
    $(wildcard include/config/PAGE_PINNER) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/stacktrace.h \
    $(wildcard include/config/STACKTRACE) \
    $(wildcard include/config/ARCH_STACKWALK) \
    $(wildcard include/config/HAVE_RELIABLE_STACKTRACE) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/stackdepot.h \
    $(wildcard include/config/STACKDEPOT) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/page_ref.h \
    $(wildcard include/config/DEBUG_PAGE_REF) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/memremap.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/huge_mm.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/sched/coredump.h \
    $(wildcard include/config/CORE_DUMP_DEFAULT_ELF_HEADERS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/vmstat.h \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/pm_qos.h \
    $(wildcard include/config/CPU_IDLE) \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/compress_driver.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/sound/compress_offload.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/sound/compress_params.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/control.h \
    $(wildcard include/config/SND_CTL_VALIDATION) \
    $(wildcard include/config/SND_CTL_LED) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/nospec.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/ac97_codec.h \
    $(wildcard include/config/SND_AC97_POWER_SAVE) \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/ac97/regs.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/info.h \
    $(wildcard include/config/SND_PROC_FS) \
  /home/zhi/derp/kernel/meizu/m2391/include/linux/seq_file.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/soc-dapm.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/soc-topology.h \
  /home/zhi/derp/kernel/meizu/m2391/include/uapi/sound/asoc.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/soc-dpcm.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/soc-dai.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/soc-component.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/soc-card.h \
  /home/zhi/derp/kernel/meizu/m2391/include/sound/soc-jack.h \
    $(wildcard include/config/GPIOLIB) \

/home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/meizu-dummy.o: $(deps_/home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/meizu-dummy.o)

$(deps_/home/zhi/derp/kernel/meizu/m2391-modules/meizu/audio-dummy/meizu-dummy.o):
