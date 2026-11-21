/**
 * @file main.cpp
 * @brief Entry point of the single `proofrunkit` executable (the composition root).
 *
 * The same binary is also installed as `prk.exe` (#60); argv[0] only changes the name shown in
 * help and error messages.
 *
 * Owns the process-wide pieces (registry, clock, standard streams) and hands them to the shared
 * command-line front end. Tests are loaded at runtime; none are compiled into this binary (#22).
 */
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "cpp/builtin_checks.hpp"
#include "cpp/command_line.hpp"
#include "cpp/command_line_app.hpp"
#include "cpp/suite/test_command.hpp"

int main(int argc, char** argv) {
    const std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)> registry(prk_create_registry(),
                                                                                 &prk_destroy_registry);
    if (!registry) {
        std::cerr << "proofrunkit: out of memory\n";
        return prk::kExitTestFailed;
    }
    const PrkStatus status = prk::register_builtin_checks(*registry);
    if (status != PRK_STATUS_OK) {
        std::cerr << "proofrunkit: could not register built-in checks: " << prk_get_status_name(status) << '\n';
        return prk::kExitTestFailed;
    }
    const std::string_view program_name = prk::select_program_name(argc > 0 ? argv[0] : "");
    const std::vector<std::string_view> arguments(argv + (argc > 0 ? 1 : 0), argv + argc);
    const prk::UpdateServices update_services = prk::make_system_update_services(std::cin);
    const bool is_offline = prk::is_offline_requested_by_environment();
    const prk::TestSuiteServices test_suite_services = prk::make_system_test_suite_services();
    const prk::CommandLineEnvironment environment{*registry,
                                                  prk::make_cycle_counter_clock(),
                                                  &prk::read_script_file_from_disk,
                                                  prk::make_disk_script_host_services(),
                                                  &prk::write_report_file_to_disk,
                                                  std::cout,
                                                  std::cerr,
                                                  is_offline ? nullptr : &update_services,
                                                  &test_suite_services};
    return prk::run_command_line(program_name, arguments, environment);
}
