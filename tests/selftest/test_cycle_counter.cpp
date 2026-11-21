/**
 * @file test_cycle_counter.cpp
 * @brief Tests for prk_read_cycle_counter (Assembly where built) against prk_read_cycle_counter_fallback (#8).
 *
 * The two counters tick at different rates (TSC vs QueryPerformanceCounter / CLOCK_MONOTONIC), so the
 * comparison checks shared contracts: non-zero, non-decreasing, and both advance over the same interval.
 */
#include <cstdint>
#include <cstring>

#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

using ReadCounter = std::uint64_t (*)();

/** @brief Returns true when @p read_counter never goes backwards over @p sample_count reads. */
bool is_counter_non_decreasing(ReadCounter read_counter, int sample_count) {
    std::uint64_t previous = read_counter();
    for (int sample = 0; sample < sample_count; ++sample) {
        const std::uint64_t current = read_counter();
        if (current < previous) {
            return false;
        }
        previous = current;
    }
    return true;
}

/** @brief Spins until @p read_counter changes from @p start or the attempt budget runs out. */
std::uint64_t wait_for_counter_change(ReadCounter read_counter, std::uint64_t start) {
    std::uint64_t latest = start;
    for (long attempt = 0; attempt < 50000000L && latest == start; ++attempt) {
        latest = read_counter();
    }
    return latest;
}

void test_reads_are_nonzero(PrkTestContext* context, void*) {
    PRK_EXPECT(prk_read_cycle_counter() != 0);
    PRK_EXPECT(prk_read_cycle_counter_fallback() != 0);
}

void test_reads_are_monotonic(PrkTestContext* context, void*) {
    PRK_EXPECT(is_counter_non_decreasing(&prk_read_cycle_counter, 100000));
    PRK_EXPECT(is_counter_non_decreasing(&prk_read_cycle_counter_fallback, 100000));
}

void test_assembly_and_fallback_advance_together(PrkTestContext* context, void*) {
    const std::uint64_t primary_start = prk_read_cycle_counter();
    const std::uint64_t fallback_start = prk_read_cycle_counter_fallback();
    const std::uint64_t fallback_end = wait_for_counter_change(&prk_read_cycle_counter_fallback, fallback_start);
    const std::uint64_t primary_end = prk_read_cycle_counter();
    PRK_EXPECT(fallback_end > fallback_start);
    PRK_EXPECT(primary_end > primary_start);
}

void test_implementation_name_matches_selection(PrkTestContext* context, void*) {
    const char* const name = prk_get_cycle_counter_implementation_name();
    PRK_EXPECT(name != nullptr && name[0] != '\0');
    const bool named_as_assembly = std::strncmp(name, "asm-", 4) == 0;
    PRK_EXPECT(named_as_assembly == (prk_is_cycle_counter_assembly() == 1));
    PRK_EXPECT(named_as_assembly || std::strcmp(name, "c-fallback") == 0);
#if defined(_MSC_VER) && defined(_M_X64) && !defined(PRK_EXPECT_C_FALLBACK)
    PRK_EXPECT(std::strcmp(name, "asm-masm-x64") == 0);
#endif
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(PRK_EXPECT_C_FALLBACK)
    PRK_EXPECT(std::strcmp(name, "asm-masm-x86") == 0);
#endif
}

void test_assembly_is_in_use(PrkTestContext* context, void*) {
    if (!prk_is_cycle_counter_assembly()) {
        prk_skip_test(context, "Assembly not built for this target; C fallback in use");
    }
}

}  // namespace

PrkStatus register_cycle_counter_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"hotpath.cycle_counter.reads_are_nonzero", &test_reads_are_nonzero, nullptr},
        {"hotpath.cycle_counter.reads_are_monotonic", &test_reads_are_monotonic, nullptr},
        {"hotpath.cycle_counter.assembly_and_fallback_advance_together", &test_assembly_and_fallback_advance_together,
         nullptr},
        {"hotpath.cycle_counter.implementation_name_matches_selection", &test_implementation_name_matches_selection,
         nullptr},
        {"hotpath.cycle_counter.assembly_is_in_use", &test_assembly_is_in_use, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
