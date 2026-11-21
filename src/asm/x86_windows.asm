; ---------------------------------------------------------------------------------------------------
; ProofRunKit hot path: prk_read_cycle_counter for Windows x86 (MASM, assembled by ml.exe).
;
; C prototype : uint64_t prk_read_cycle_counter(void);          (include/proofrunkit/prk.h)
; C fallback  : prk_read_cycle_counter_fallback                  (src/timing/prk_cycle_counter_fallback.c)
;               selected instead of this file when PRK_FORCE_C_FALLBACK=ON or MASM is unavailable.
; ABI         : cdecl (32-bit). `.model flat, C` makes PROC emit the leading underscore
;               (_prk_read_cycle_counter) that MSVC cdecl expects. No arguments; a 64-bit
;               integer is returned in EDX:EAX.
; Instruction : RDTSC already writes the 64-bit time-stamp counter to EDX:EAX, so the
;               cdecl return is just rdtsc / ret. EAX and EDX are cdecl-volatile; nothing
;               is saved. Leaf function: no stack allocation, no pushes, and no calls.
; SAFESEH     : CMake passes /safeseh to ml.exe so the object can be linked into an MSVC
;               32-bit image that uses /SAFESEH (the toolchain default).
; Test        : tests/selftest/test_cycle_counter.cpp compares this routine with the C
;               fallback and asserts the implementation name "asm-masm-x86" on MSVC Win32.
; ---------------------------------------------------------------------------------------------------

.686
.model flat, C
.code

prk_read_cycle_counter PROC
    rdtsc
    ret
prk_read_cycle_counter ENDP

END
