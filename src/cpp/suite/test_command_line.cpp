/**
 * @file test_command_line.cpp
 * @brief Implementation of `prk test` parsing and help (#82).
 */
#include "test_command_line.hpp"

#include <utility>

#include "test_config.hpp"

namespace prk {

namespace {

/** @brief Builds a parse result that carries only an error. */
CommandLineParseResult make_suite_error(std::string message) {
    return CommandLineParseResult{CommandLineOptions{}, std::move(message)};
}

/** @brief Applies one option that takes a value; returns "" or the usage error. */
std::string apply_suite_option(std::string_view option, std::string_view value, CommandLineOptions& options) {
    if (value.empty()) {
        return std::string(option) + " needs a non-empty value";
    }
    if (option == "--config") {
        options.suite_config_path = std::string(value);
    } else if (option == "--build-dir") {
        options.suite_build_dir = std::string(value);
    } else if (option == "--var") {
        const std::size_t equals = value.find('=');
        if (equals == std::string_view::npos || !is_valid_test_entry_name(value.substr(0, equals))) {
            return "--var needs NAME=VALUE (got '" + std::string(value) + "')";
        }
        options.suite_variables.emplace_back(value);
    } else if (option == "--filter") {
        options.name_filter = std::string(value);
    } else if (option == "--json-file") {
        options.json_file = std::string(value);
    } else if (option == "--junit-file") {
        options.junit_file = std::string(value);
    } else if (value == "console" || value == "json" || value == "junit") {
        options.report_format = value == "console" ? ReportFormat::Console
                                : value == "json"  ? ReportFormat::Json
                                                   : ReportFormat::Junit;
    } else {
        return "unknown " + std::string(option) + " '" + std::string(value) + "' (use console, json, or junit)";
    }
    return {};
}

/** @brief Returns true when @p argument is a `prk test` option that takes a value. */
bool is_suite_option_with_value(std::string_view argument) {
    return argument == "--config" || argument == "--build-dir" || argument == "--var" || argument == "--filter" ||
           argument == "--json-file" || argument == "--junit-file" || argument == "--format" ||
           argument == "--reporter";
}

}  // namespace

CommandLineParseResult parse_suite_command(std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    options.command = CliCommand::Test;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
            options.help_topic = "test";
        } else if (argument == "--list") {
            options.list_tests = true;
        } else if (argument == "--json") {
            options.report_format = ReportFormat::Json;
        } else if (is_suite_option_with_value(argument)) {
            if (index + 1 >= arguments.size()) {
                return make_suite_error(std::string(argument) + " needs a value");
            }
            std::string problem = apply_suite_option(argument, arguments[++index], options);
            if (!problem.empty()) {
                return make_suite_error(std::move(problem));
            }
        } else {
            return make_suite_error("unknown test option '" + std::string(argument) + "' (see help test)");
        }
    }
    if (options.list_tests && (!options.json_file.empty() || !options.junit_file.empty())) {
        return make_suite_error("--json-file and --junit-file apply to runs, not listings");
    }
    return CommandLineParseResult{std::move(options), {}};
}

std::string format_suite_help_text(std::string_view program_name) {
    const std::string name(program_name);
    return "Usage: " + name + " test [options]\n\n" +
           "Runs the project's tests as described by prk.tests.json in the current folder: setup steps,\n"
           "then every test entry (Lua/JS scripts run by a prk runner, or any program), each with its own\n"
           "folder, arguments, environment, and expected exit code and output. Prints one combined summary.\n"
           "Entries that need an unset variable (such as ${build_dir}) are skipped. See docs/test-config.md.\n"
           "\n"
           "Options:\n"
           "      --config FILE       Use FILE instead of ./prk.tests.json.\n"
           "      --build-dir DIR     Set ${build_dir} (relative to the current folder).\n"
           "      --var NAME=VALUE    Set or override a variable (repeatable).\n"
           "      --filter TEXT       Run only test entries whose name contains TEXT (setup always runs).\n"
           "      --list              List the setup steps and test entries without running them.\n"
           "      --reporter FORMAT   Combined result format on stdout: console (default), json, or junit.\n"
           "      --json              Same as --reporter json.\n"
           "      --json-file PATH    Also write the combined JSON report to PATH.\n"
           "      --junit-file PATH   Also write the combined JUnit XML report to PATH.\n"
           "  -h, --help              Show this help.\n"
           "\n"
           "Exit codes: 0 all passed, 1 a test failed, 2 usage error (or no prk.tests.json),\n"
           "            4 the configuration is invalid, 5 a report file could not be written.\n";
}

}  // namespace prk
