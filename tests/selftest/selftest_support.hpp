/**
 * @file selftest_support.hpp
 * @brief Assertion macro and suite registration functions for proofrunkit_selftest.
 */
#pragma once

#include <string>

#include "proofrunkit/prk.h"

/**
 * @brief Fails the current test with the file, line, and expression text, then returns from the body.
 *
 * Usable only inside a test body whose context parameter is named @c context.
 */
#define PRK_EXPECT(condition)                                                                                   \
    do {                                                                                                        \
        if (!(condition)) {                                                                                     \
            prk_fail_test(                                                                                      \
                context,                                                                                        \
                (std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": expected " + #condition).c_str()); \
            return;                                                                                             \
        }                                                                                                       \
    } while (false)

namespace prk::selftest {

/** @brief Registers registry tests. @return PRK_STATUS_OK or the first registration error. */
PrkStatus register_registry_tests(PrkRegistry& registry);
/** @brief Registers runner tests. @return PRK_STATUS_OK or the first registration error. */
PrkStatus register_runner_tests(PrkRegistry& registry);
/** @brief Registers reporter and summary tests. @return PRK_STATUS_OK or the first registration error. */
PrkStatus register_reporting_tests(PrkRegistry& registry);
/** @brief Registers command-line parsing and front-end tests. @return PRK_STATUS_OK or the first error. */
PrkStatus register_command_line_tests(PrkRegistry& registry);
/** @brief Registers cycle-counter (Assembly vs C fallback) tests. @return PRK_STATUS_OK or the first error. */
PrkStatus register_cycle_counter_tests(PrkRegistry& registry);
/** @brief Registers sandboxed Lua engine tests (#9). @return PRK_STATUS_OK or the first error. */
PrkStatus register_lua_engine_tests(PrkRegistry& registry);
/** @brief Registers the JUnit XML and report-file tests (#21). */
PrkStatus register_junit_reporting_tests(PrkRegistry& registry);
/** @brief Registers the Lua test-authoring API tests (#12). */
PrkStatus register_lua_test_api_tests(PrkRegistry& registry);
/** @brief Registers QuickJS-ng engine and JavaScript API tests (#11). @return PRK_STATUS_OK or the first error. */
PrkStatus register_js_engine_tests(PrkRegistry& registry);
/** @brief Registers process-spawn tests (#13). @return PRK_STATUS_OK or the first registration error. */
PrkStatus register_process_spawn_tests(PrkRegistry& registry);
/** @brief Registers script process-API tests (#12). @return PRK_STATUS_OK or the first registration error. */
PrkStatus register_script_process_api_tests(PrkRegistry& registry);
/** @brief Registers C/C++ compiler-driver tests (#14). @return PRK_STATUS_OK or the first registration error. */
PrkStatus register_c_driver_tests(PrkRegistry& registry);
/** @brief Registers C/C++ driver library and MSVC discovery tests (#14). @return PRK_STATUS_OK or the first error. */
PrkStatus register_c_driver_toolchain_tests(PrkRegistry& registry);
/** @brief Registers test-script loading tests (#9). @return PRK_STATUS_OK or the first error. */
PrkStatus register_script_loading_tests(PrkRegistry& registry);

/**
 * @brief Registers each test case in order, stopping at the first error.
 * @param registry Registry to add to.
 * @param test_cases Test cases to add.
 * @return PRK_STATUS_OK or the first registration error.
 */
template <std::size_t Count>
PrkStatus register_test_cases(PrkRegistry& registry, const PrkTestCase (&test_cases)[Count]) {
    for (const PrkTestCase& test_case : test_cases) {
        const PrkStatus status = prk_register_test_case(&registry, &test_case);
        if (status != PRK_STATUS_OK) {
            return status;
        }
    }
    return PRK_STATUS_OK;
}

}  // namespace prk::selftest
