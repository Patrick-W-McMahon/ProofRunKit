/**
 * @file command_line.hpp
 * @brief Pure parsing of the proofrunkit command line into an immutable options value.
 */
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prk {

/** @brief Output format for test results. */
enum class ReportFormat { Console, Json };

/** @brief Options parsed from the command line. */
struct CommandLineOptions {
    bool show_help = false;                              ///< --help / -h
    bool show_version = false;                           ///< --version
    bool list_tests = false;                             ///< --list: print selected test names, run nothing
    ReportFormat report_format = ReportFormat::Console;  ///< --format console|json
    std::string name_filter;                             ///< --filter TEXT: run tests whose name contains TEXT
    std::vector<std::string> script_paths;               ///< Positional test scripts (Lua/JS engines: #9, #11)
};

/** @brief Result of parsing: either options or a usage error message. */
struct CommandLineParseResult {
    CommandLineOptions options;  ///< Valid only when error_message is empty.
    std::string error_message;   ///< Non-empty when the command line is invalid.
};

/**
 * @brief Parses arguments (excluding the program name). Pure function.
 * @param arguments Arguments in order, e.g. {"--filter", "core."}.
 * @return Parsed options, or a non-empty error_message describing the first problem found.
 */
[[nodiscard]] CommandLineParseResult parse_command_line(std::span<const std::string_view> arguments);

/**
 * @brief Builds the --help text. Pure function.
 * @param program_name Name shown in the usage line.
 * @return Multi-line usage text ending in a newline.
 */
[[nodiscard]] std::string format_usage_text(std::string_view program_name);

}  // namespace prk
