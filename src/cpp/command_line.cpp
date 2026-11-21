/**
 * @file command_line.cpp
 * @brief Implementation of prk::parse_command_line and prk::format_usage_text.
 */
#include "command_line.hpp"

#include <optional>
#include <utility>

namespace prk {

namespace {

/** @brief Parses a --format value; returns std::nullopt when the value is not recognised. */
std::optional<ReportFormat> parse_report_format(std::string_view value) {
    if (value == "console") {
        return ReportFormat::Console;
    }
    if (value == "json") {
        return ReportFormat::Json;
    }
    return std::nullopt;
}

/** @brief Builds a parse result that carries only an error. */
CommandLineParseResult make_parse_error(std::string message) {
    return CommandLineParseResult{CommandLineOptions{}, std::move(message)};
}

}  // namespace

CommandLineParseResult parse_command_line(std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        const bool has_value = index + 1 < arguments.size();
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if (argument == "--version") {
            options.show_version = true;
        } else if (argument == "--list") {
            options.list_tests = true;
        } else if (argument == "--filter" || argument == "--format") {
            if (!has_value) {
                return make_parse_error(std::string(argument) + " needs a value");
            }
            const std::string_view value = arguments[++index];
            if (argument == "--filter") {
                options.name_filter = std::string(value);
            } else if (const std::optional<ReportFormat> format = parse_report_format(value)) {
                options.report_format = *format;
            } else {
                return make_parse_error("unknown --format '" + std::string(value) + "' (use console or json)");
            }
        } else if (argument.starts_with("-")) {
            return make_parse_error("unknown option '" + std::string(argument) + "'");
        } else {
            options.script_paths.emplace_back(argument);
        }
    }
    return CommandLineParseResult{std::move(options), {}};
}

std::string format_usage_text(std::string_view program_name) {
    std::string text = "Usage: ";
    text += program_name;
    text +=
        " [options] [test scripts...]\n"
        "\n"
        "Options:\n"
        "  -h, --help           Show this help and exit.\n"
        "      --version        Show the version and the cycle-counter implementation, then exit.\n"
        "      --list           List the selected test names without running them.\n"
        "      --filter TEXT    Run only tests whose name contains TEXT.\n"
        "      --format FORMAT  Result format: console (default) or json.\n"
        "\n"
        "Exit codes: 0 all selected tests passed, 1 a test failed, 2 usage error,\n"
        "            3 requested feature not implemented yet (e.g. Lua/JS test scripts).\n";
    return text;
}

}  // namespace prk
