/**
 * @file prk_cycle_counter_fallback.c
 * @brief Portable C fallback for the prk_read_cycle_counter Assembly hot path (#8).
 *
 * Always compiled, so the self-test can compare the Assembly routine with this fallback.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L /* clock_gettime under strict -std=c17 */
#endif

#include "proofrunkit/prk.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

uint64_t prk_read_cycle_counter_fallback(void) {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter); /* Cannot fail on Windows XP and later. */
    return (uint64_t)counter.QuadPart;
}
#else
#include <time.h>

uint64_t prk_read_cycle_counter_fallback(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 1; /* Keep the documented non-zero contract even if the clock is unavailable. */
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec + 1;
}
#endif
