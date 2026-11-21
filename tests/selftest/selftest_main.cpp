/**
 * @file selftest_main.cpp
 * @brief Entry point of proofrunkit_selftest. Accepts the same options as proofrunkit
 *        (--list, --filter, --format json).
 */
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "cpp/command_line_app.hpp"
#include "selftest_support.hpp"

namespace {

/** @brief Registers every self-test suite. @return PRK_STATUS_OK or the first registration error. */
PrkStatus register_all_selftests(PrkRegistry& registry) {
    using RegisterSuite = PrkStatus (*)(PrkRegistry&);
    const RegisterSuite suites[] = {
        &prk::selftest::register_registry_tests,        &prk::selftest::register_runner_tests,
        &prk::selftest::register_reporting_tests,       &prk::selftest::register_command_line_tests,
        &prk::selftest::register_cycle_counter_tests,   &prk::selftest::register_lua_engine_tests,
        &prk::selftest::register_script_loading_tests,  &prk::selftest::register_lua_test_api_tests,
        &prk::selftest::register_junit_reporting_tests, &prk::selftest::register_js_engine_tests,
        &prk::selftest::register_process_spawn_tests,   &prk::selftest::register_script_process_api_tests,
        &prk::selftest::register_c_driver_tests,        &prk::selftest::register_c_driver_toolchain_tests,
    };
    for (const RegisterSuite register_suite : suites) {
        const PrkStatus status = register_suite(registry);
        if (status != PRK_STATUS_OK) {
            return status;
        }
    }
    return PRK_STATUS_OK;
}

}  // namespace

int main(int argc, char** argv) {
    const std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)> registry(prk_create_registry(),
                                                                                 &prk_destroy_registry);
    if (!registry) {
        std::cerr << "proofrunkit_selftest: out of memory\n";
        return prk::kExitTestFailed;
    }
    const PrkStatus status = register_all_selftests(*registry);
    if (status != PRK_STATUS_OK) {
        std::cerr << "proofrunkit_selftest: could not register tests: " << prk_get_status_name(status) << '\n';
        return prk::kExitTestFailed;
    }
    std::cout << "ProofRunKit " << prk_get_version_string()
              << " self-test (cycle counter: " << prk_get_cycle_counter_implementation_name() << ")\n";
    const std::vector<std::string_view> arguments(argv + (argc > 0 ? 1 : 0), argv + argc);
    const prk::CommandLineEnvironment environment{*registry,
                                                  prk::make_cycle_counter_clock(),
                                                  &prk::read_script_file_from_disk,
                                                  prk::make_disk_script_host_services(),
                                                  &prk::write_report_file_to_disk,
                                                  std::cout,
                                                  std::cerr};
    return prk::run_command_line("proofrunkit_selftest", arguments, environment);
}
