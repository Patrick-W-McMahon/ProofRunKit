/*
 * ProofRunKit hot path: prk_read_cycle_counter for arm64 (AAPCS64; Linux ELF, macOS Mach-O), GAS syntax.
 *
 * C prototype : uint64_t prk_read_cycle_counter(void);          (include/proofrunkit/prk.h)
 * C fallback  : prk_read_cycle_counter_fallback                  (src/timing/prk_cycle_counter_fallback.c)
 * ABI         : AAPCS64. No arguments; result in x0 (caller-saved). Leaf, no stack use.
 * Note        : reads the virtual counter CNTVCT_EL0, which user space may read on Linux and macOS.
 *               Untested platform until Linux/macOS testing lands (#34, #35).
 */
#if defined(__APPLE__)
#define PRK_SYMBOL(name) _##name
#else
#define PRK_SYMBOL(name) name
#endif

    .text
    .globl PRK_SYMBOL(prk_read_cycle_counter)
#if !defined(__APPLE__)
    .type prk_read_cycle_counter, %function
#endif
    .p2align 2
PRK_SYMBOL(prk_read_cycle_counter):
    isb                         /* stop the counter read from being hoisted above earlier work */
    mrs     x0, cntvct_el0
    ret
#if !defined(__APPLE__)
    .size prk_read_cycle_counter, .-prk_read_cycle_counter
    .section .note.GNU-stack, "", %progbits
#endif
