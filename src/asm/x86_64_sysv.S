/*
 * ProofRunKit hot path: prk_read_cycle_counter for x86-64 System V (Linux ELF, macOS Mach-O), GAS syntax.
 *
 * C prototype : uint64_t prk_read_cycle_counter(void);          (include/proofrunkit/prk.h)
 * C fallback  : prk_read_cycle_counter_fallback                  (src/timing/prk_cycle_counter_fallback.c)
 * ABI         : System V AMD64. No arguments; result in %rax. Uses only %rax and %rdx (caller-saved).
 * Frame       : leaf, no stack use. Untested platform until Linux/macOS testing lands (#34, #35).
 */
#if defined(__APPLE__)
#define PRK_SYMBOL(name) _##name
#else
#define PRK_SYMBOL(name) name
#endif

    .text
    .globl PRK_SYMBOL(prk_read_cycle_counter)
#if !defined(__APPLE__)
    .type prk_read_cycle_counter, @function
#endif
PRK_SYMBOL(prk_read_cycle_counter):
#if defined(__CET__)
    endbr64                     /* keep the object IBT-compatible when built with -fcf-protection */
#endif
    rdtsc                       /* EDX:EAX = time-stamp counter; upper halves are zero-extended */
    shlq    $32, %rdx
    orq     %rdx, %rax
    ret
#if !defined(__APPLE__)
    .size prk_read_cycle_counter, .-prk_read_cycle_counter
    .section .note.GNU-stack, "", @progbits
#endif
