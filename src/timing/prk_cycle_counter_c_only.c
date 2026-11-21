/**
 * @file prk_cycle_counter_c_only.c
 * @brief Defines prk_read_cycle_counter as the C fallback. CMake compiles this file only when no
 *        Assembly implementation is selected (PRK_FORCE_C_FALLBACK=ON or an unsupported target).
 */
#include "proofrunkit/prk.h"

uint64_t prk_read_cycle_counter(void) {
    return prk_read_cycle_counter_fallback();
}
