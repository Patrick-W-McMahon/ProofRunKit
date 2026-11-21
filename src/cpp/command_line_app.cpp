/**
 * @file command_line_app.cpp
 * @brief Implementation of prk::run_command_line (flag form and `prk` subcommands, #60).
 */
#include "command_line_app.hpp"

#include <cstdint>
#include <string>

#include "command_line.hpp"
#include "reporters.hpp"

namespace prk {

namespace {

/** @brief Adapts prk_read_cycle_counter() to the PrkClock callback signature. */
std::uint64_t read_cycle_counter_ticks(void* /*clock_state*/) {
    return prk_read_cycle_counter();
}

/** @brief Writes the selected test names, one per line. */
void write_selected_test_names(const PrkRegistry& registry, std::string_view name_filter, std::ostream& output) {
    const std::string filter(name_filter);
    const std::size_t count = prk_count_test_cases(&registry);
    for (std::size_t index = 0; index < count; ++index) {
        const char* const name = prk_get_test_case_name(&registry, index);
        if (prk_is_test_name_selected(name, filter.c_str())) {
            output << name << '\n';
        }
    }
}

/** @brief Runs the selected tests with the chosen reporter and returns the exit code. */
int run_selected_tests(const CommandLineOptions& options, const PrkRegistry& registry, const PrkClock& clock,
                       std::ostream& output) {
    const PrkRunOptions run_options{options.name_filter.c_str(), clock};
    ConsoleReporter console_reporter(output);
    JsonReporter json_reporter(output);
    const PrkReporter reporter = options.report_format == ReportFormat::Json ? json_reporter.make_c_reporter()
                                                                             : console_reporter.make_c_reporter();
    PrkRunSummary summary{};
    if (prk_run_test_cases(&registry, &run_options, &reporter, &summary) != PRK_STATUS_OK) {
        return kExitTestFailed;
    }
    return summary.failed == 0 ? kExitAllPassed : kExitTestFailed;
}

}  // namespace

int run_command_line(std::string_view program_name, std::span<const std::string_view> arguments,
                     const PrkRegistry& registry, const PrkClock& clock, std::ostream& output, std::ostream& errors) {
    const CommandLineParseResult parsed = parse_command_line(arguments);
    if (!parsed.error_message.empty()) {
        errors << program_name << ": " << parsed.error_message << "\n\n" << format_usage_text(program_name);
        return kExitUsageError;
    }
    const CommandLineOptions& options = parsed.options;
    if (options.show_help) {
        output << (options.help_topic.empty() ? format_usage_text(program_name)
                                              : format_command_help_text(program_name, options.help_topic));
        return kExitAllPassed;
    }
    if (options.show_version) {
        output << "ProofRunKit " << prk_get_version_string()
               << " (cycle counter: " << prk_get_cycle_counter_implementation_name() << ")\n";
        return kExitAllPassed;
    }
    if (!options.script_paths.empty()) {
        // Seam for the script engines: Lua 5.4 (#9) and QuickJS-ng (#11) will load these at runtime.
        errors << program_name << ": running test scripts is not implemented yet (Lua: #9, JavaScript: #11).\n";
        return kExitNotImplemented;
    }
    if (options.list_tests) {
        write_selected_test_names(registry, options.name_filter, output);
        return kExitAllPassed;
    }
    return run_selected_tests(options, registry, clock, output);
}

PrkClock make_cycle_counter_clock() {
    return PrkClock{&read_cycle_counter_ticks, nullptr};
}

}  // namespace prk
