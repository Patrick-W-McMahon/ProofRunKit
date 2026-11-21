/**
 * @file main.cpp
 * @brief Entry point of the single `proofrunkit` executable (the composition root).
 *
 * Owns the process-wide pieces (registry, clock, standard streams) and hands them to the shared
 * command-line front end. Tests are loaded at runtime; none are compiled into this binary (#22).
 */
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "cpp/builtin_checks.hpp"
#include "cpp/command_line_app.hpp"

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
    const std::vector<std::string_view> arguments(argv + (argc > 0 ? 1 : 0), argv + argc);
    return prk::run_command_line("proofrunkit", arguments, *registry, prk::make_cycle_counter_clock(), std::cout,
                                 std::cerr);
}
