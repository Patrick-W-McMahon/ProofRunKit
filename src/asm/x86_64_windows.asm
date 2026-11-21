; ---------------------------------------------------------------------------------------------------
; ProofRunKit hot path: prk_read_cycle_counter for Windows x64 (MASM, assembled by ml64.exe).
;
; C prototype : uint64_t prk_read_cycle_counter(void);          (include/proofrunkit/prk.h)
; C fallback  : prk_read_cycle_counter_fallback                  (src/timing/prk_cycle_counter_fallback.c)
;               selected instead of this file when PRK_FORCE_C_FALLBACK=ON or MASM is unavailable.
; ABI         : Microsoft x64. No arguments; the 64-bit result is returned in RAX.
;
; Register use: RAX and RDX only. Both are volatile in the Win64 ABI, so nothing is saved.
; Frame       : leaf function with no stack allocation, no pushes, and no calls. RSP is never
;               changed, so Win64 requires no prologue and no unwind data (.pdata/.xdata).
; Test        : tests/selftest/test_cycle_counter.cpp compares this routine with the C fallback.
; ---------------------------------------------------------------------------------------------------

.code

prk_read_cycle_counter PROC
    ; RDTSC writes the 64-bit time-stamp counter split across EDX:EAX. Writing a 32-bit register
    ; zero-extends into the full 64-bit register, so the upper halves of RAX and RDX are already 0.
    rdtsc
    ; Recombine the halves into RAX = (RDX << 32) | RAX for the uint64_t return value.
    shl     rdx, 32
    or      rax, rdx
    ret
prk_read_cycle_counter ENDP

END
