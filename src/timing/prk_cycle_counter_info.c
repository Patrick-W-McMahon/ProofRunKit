/**
 * @file prk_cycle_counter_info.c
 * @brief Reports which cycle-counter implementation CMake selected (PRK_CYCLE_COUNTER_IMPLEMENTATION).
 */
#include <string.h>

#include "proofrunkit/prk.h"

#ifndef PRK_CYCLE_COUNTER_IMPLEMENTATION
#error "PRK_CYCLE_COUNTER_IMPLEMENTATION must be defined by the build (see CMakeLists.txt)."
#endif

const char* prk_get_cycle_counter_implementation_name(void) {
    return PRK_CYCLE_COUNTER_IMPLEMENTATION;
}

int prk_is_cycle_counter_assembly(void) {
    return strncmp(PRK_CYCLE_COUNTER_IMPLEMENTATION, "asm-", 4) == 0;
}
