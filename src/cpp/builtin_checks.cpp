/**
 * @file builtin_checks.cpp
 * @brief Built-in environment checks: confirm the timing hot path works on this machine.
 */
#include "builtin_checks.hpp"

#include <cstdint>

namespace prk {

namespace {

/** @brief Fails unless the cycle counter returns a non-zero value. */
void check_cycle_counter_reads_nonzero(PrkTestContext* context, void* /*user_state*/) {
    if (prk_read_cycle_counter() == 0) {
        prk_fail_test(context, "prk_read_cycle_counter returned 0");
    }
}

/** @brief Fails unless the cycle counter advances across a short busy loop. */
void check_cycle_counter_advances(PrkTestContext* context, void* /*user_state*/) {
    const std::uint64_t first = prk_read_cycle_counter();
    std::uint64_t latest = first;
    for (int attempt = 0; attempt < 1000000 && latest == first; ++attempt) {
        latest = prk_read_cycle_counter();
    }
    if (latest <= first) {
        prk_fail_test(context, "prk_read_cycle_counter did not advance");
    }
}

}  // namespace

PrkStatus register_builtin_checks(PrkRegistry& registry) {
    const PrkTestCase checks[] = {
        {"kit.cycle_counter.reads_nonzero", &check_cycle_counter_reads_nonzero, nullptr},
        {"kit.cycle_counter.advances", &check_cycle_counter_advances, nullptr},
    };
    for (const PrkTestCase& check : checks) {
        const PrkStatus status = prk_register_test_case(&registry, &check);
        if (status != PRK_STATUS_OK) {
            return status;
        }
    }
    return PRK_STATUS_OK;
}

}  // namespace prk
