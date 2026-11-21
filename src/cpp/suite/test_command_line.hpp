/**
 * @file test_command_line.hpp
 * @brief Parsing and help text of `prk test` (#82). Pure; kept apart from command_line.cpp for its size.
 */
#pragma once

#include <span>
#include <string>
#include <string_view>

#include "cpp/command_line.hpp"

namespace prk {

/**
 * @brief Parses the arguments after `test`. Pure function.
 * @param arguments e.g. {"--build-dir", "build", "--var", "x=1", "--filter", "self."}.
 * @return Options with command Test, or a usage error.
 */
[[nodiscard]] CommandLineParseResult parse_suite_command(std::span<const std::string_view> arguments);

/**
 * @brief Builds the help text of `prk test`. Pure function.
 * @param program_name Name shown in the usage line. @return Multi-line text ending in a newline.
 */
[[nodiscard]] std::string format_suite_help_text(std::string_view program_name);

}  // namespace prk
