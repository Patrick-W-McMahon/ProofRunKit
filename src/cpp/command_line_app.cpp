/**
 * @file command_line_app.cpp
 * @brief Implementation of prk::run_command_line (flag form and `prk` subcommands, #60; Lua scripts, #9;
 *        JavaScript scripts, #11).
 */
#include "command_line_app.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "command_line.hpp"
#include "junit_reporter.hpp"
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

/** @brief Returns the JUnit wall clock: the script host clock in seconds, or none. */
SecondsClock make_report_seconds_clock(const CommandLineEnvironment& environment) {
    if (!environment.script_services.read_monotonic_milliseconds) {
        return {};
    }
    return [read_milliseconds = environment.script_services.read_monotonic_milliseconds] {
        return static_cast<double>(read_milliseconds()) / 1000.0;
    };
}

/** @brief Writes one report file; returns false (after an error line) when it cannot be written. */
bool write_report_file(std::string_view program_name, const std::string& path, const std::string& text,
                       const CommandLineEnvironment& environment) {
    std::string problem = environment.write_report_file ? environment.write_report_file(path, text)
                                                        : std::string("report files are not available");
    if (problem.empty()) {
        return true;
    }
    environment.errors << program_name << ": cannot write report '" << path << "': " << problem << '\n';
    return false;
}

/** @brief Runs the selected tests with the stdout reporter plus any report files; returns the exit code. */
int run_selected_tests(std::string_view program_name, const CommandLineOptions& options, const PrkRegistry& registry,
                       const CommandLineEnvironment& environment) {
    const PrkRunOptions run_options{options.name_filter.c_str(), environment.clock};
    const SecondsClock seconds_clock = make_report_seconds_clock(environment);
    ConsoleReporter console_reporter(environment.output);
    JsonReporter json_reporter(environment.output);
    JUnitReporter junit_reporter(environment.output, seconds_clock);
    std::ostringstream json_file_text;
    std::ostringstream junit_file_text;
    JsonReporter json_file_reporter(json_file_text);
    JUnitReporter junit_file_reporter(junit_file_text, seconds_clock);
    ReporterFanOut reporters;
    switch (options.report_format) {
        case ReportFormat::Console:
            reporters.add_reporter(console_reporter.make_c_reporter());
            break;
        case ReportFormat::Json:
            reporters.add_reporter(json_reporter.make_c_reporter());
            break;
        case ReportFormat::Junit:
            reporters.add_reporter(junit_reporter.make_c_reporter());
            break;
    }
    if (!options.json_file.empty()) {
        reporters.add_reporter(json_file_reporter.make_c_reporter());
    }
    if (!options.junit_file.empty()) {
        reporters.add_reporter(junit_file_reporter.make_c_reporter());
    }
    const PrkReporter reporter = reporters.make_c_reporter();
    PrkRunSummary summary{};
    const PrkStatus status = prk_run_test_cases(&registry, &run_options, &reporter, &summary);
    bool are_reports_written = true;
    if (!options.json_file.empty()) {
        are_reports_written &= write_report_file(program_name, options.json_file, json_file_text.str(), environment);
    }
    if (!options.junit_file.empty()) {
        are_reports_written &= write_report_file(program_name, options.junit_file, junit_file_text.str(), environment);
    }
    if (!are_reports_written) {
        return kExitReportError;
    }
    return status == PRK_STATUS_OK && summary.failed == 0 ? kExitAllPassed : kExitTestFailed;
}

/** @brief Lists or runs @p registry according to @p options. */
int list_or_run_tests(std::string_view program_name, const CommandLineOptions& options, const PrkRegistry& registry,
                      const CommandLineEnvironment& environment) {
    if (options.list_tests) {
        write_selected_test_names(registry, options.name_filter, environment.output);
        return kExitAllPassed;
    }
    return run_selected_tests(program_name, options, registry, environment);
}

/** @brief Writes "<program>: <problem>" lines for scripts that cannot run; returns the exit code or -1. */
int report_script_problems(std::string_view program_name, const LoadedTestScripts& loaded, std::ostream& errors) {
    if (!loaded.registry) {
        errors << program_name << ": out of memory loading test scripts\n";
        return kExitScriptError;
    }
    for (const std::string& path : loaded.unknown_type_scripts) {
        errors << program_name << ": " << path << ": unsupported test script type (use .lua or .js)\n";
    }
    if (!loaded.unknown_type_scripts.empty()) {
        return kExitUsageError;
    }
    for (const std::string& load_error : loaded.load_errors) {
        errors << program_name << ": " << load_error << '\n';
    }
    return loaded.load_errors.empty() ? -1 : kExitScriptError;
}

/** @brief Loads the scripts named in @p options, then lists or runs their tests. */
int list_or_run_script_tests(std::string_view program_name, const CommandLineOptions& options,
                             const CommandLineEnvironment& environment) {
    // print() must not corrupt a JSON report or a test-name listing on the output stream.
    const bool is_output_machine_read = options.report_format != ReportFormat::Console || options.list_tests;
    std::ostream& print_stream = is_output_machine_read ? environment.errors : environment.output;
    ScriptHost host(environment.script_services, print_stream);
    const PrkScriptHost c_host = host.make_c_host();
    std::vector<const char*> include_tags;
    std::vector<const char*> exclude_tags;
    for (const std::string& tag : options.include_tags) {
        include_tags.push_back(tag.c_str());
    }
    for (const std::string& tag : options.exclude_tags) {
        exclude_tags.push_back(tag.c_str());
    }
    const PrkScriptRunSettings settings{options.name_filter.c_str(), include_tags.data(), include_tags.size(),
                                        exclude_tags.data(),         exclude_tags.size(), options.default_timeout_ms};
    const LoadedTestScripts loaded =
        load_test_scripts(options.script_paths, environment.read_script_file, c_host, &settings, nullptr);
    const int problem_exit_code = report_script_problems(program_name, loaded, environment.errors);
    if (problem_exit_code >= 0) {
        return problem_exit_code;
    }
    return list_or_run_tests(program_name, options, *loaded.registry, environment);
}

/** @brief Runs `prk -update` with the environment's services. */
int run_update(std::string_view program_name, const CommandLineOptions& options,
               const CommandLineEnvironment& environment) {
    if (environment.update_services == nullptr) {
        environment.errors << program_name << " -update: updates need the network (PRK_OFFLINE is set or this "
                           << "host has no update services)\n";
        return kExitUpdateFailed;
    }
    const UpdateOptions update_options{options.is_update_check_only, options.is_update_assumed_yes,
                                       options.is_update_dry_run};
    return run_update_command(program_name, update_options, *environment.update_services, environment.output,
                              environment.errors);
}

}  // namespace

int run_command_line(std::string_view program_name, std::span<const std::string_view> arguments,
                     const CommandLineEnvironment& environment) {
    const CommandLineParseResult parsed = parse_command_line(arguments);
    if (!parsed.error_message.empty()) {
        environment.errors << program_name << ": " << parsed.error_message << "\n\n" << format_usage_text(program_name);
        return kExitUsageError;
    }
    const CommandLineOptions& options = parsed.options;
    if (options.show_help) {
        environment.output << (options.help_topic.empty() ? format_usage_text(program_name)
                                                          : format_command_help_text(program_name, options.help_topic));
        return kExitAllPassed;
    }
    if (options.show_version) {
        environment.output << "ProofRunKit " << prk_get_version_string()
                           << " (cycle counter: " << prk_get_cycle_counter_implementation_name() << ", "
                           << prk_get_lua_version_string() << ", " << prk_get_js_engine_version_string() << ")\n";
        if (!options.is_offline && environment.update_services != nullptr) {
            write_release_status(program_name, *environment.update_services, environment.output);
        }
        return kExitAllPassed;
    }
    if (options.command == CliCommand::Update) {
        return run_update(program_name, options, environment);
    }
    if (options.list_tests && (!options.json_file.empty() || !options.junit_file.empty())) {
        environment.errors << program_name << ": --json-file and --junit-file apply to runs, not listings\n";
        return kExitUsageError;
    }
    if (!options.script_paths.empty()) {
        return list_or_run_script_tests(program_name, options, environment);
    }
    if (!options.include_tags.empty() || !options.exclude_tags.empty() || options.default_timeout_ms != 0) {
        environment.errors << program_name << ": --tag, --exclude-tag, and --timeout apply only to test scripts\n";
        return kExitUsageError;
    }
    return list_or_run_tests(program_name, options, environment.builtin_registry, environment);
}

std::string write_report_file_to_disk(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return "cannot open file for writing";
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return file ? std::string() : std::string("cannot write file");
}

PrkClock make_cycle_counter_clock() {
    return PrkClock{&read_cycle_counter_ticks, nullptr};
}

}  // namespace prk
