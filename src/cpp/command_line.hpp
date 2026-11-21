/**
 * @file command_line.hpp
 * @brief Pure parsing of the `prk` / `proofrunkit` command line into an immutable options value.
 *
 * Two forms are accepted (#60):
 *   - subcommands: `prk run|list|test|version|help ...`, plus `prk -update` / `--update` / `update` and
 *     `prk -version` / `-v` (#75); `prk test` (#82) is parsed in suite/test_command_line.cpp
 *   - the original flag form: `proofrunkit [--list] [--filter TEXT] [--format FORMAT] [scripts...]`
 * Both forms produce the same CommandLineOptions. Nothing here performs IO.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prk {

/** @brief Output format for test results. */
enum class ReportFormat { Console, Json, Junit };

/** @brief Subcommand selected on the command line. */
enum class CliCommand {
    Default,  ///< No subcommand: the original `proofrunkit [options] [scripts...]` form.
    Run,      ///< `prk run`
    List,     ///< `prk list`
    Version,  ///< `prk version`
    Help,     ///< `prk help [command]`
    Update,   ///< `prk -update` / `--update` / `update` (#75)
    Test,     ///< `prk test`: run the project's prk.tests.json (#82)
};

/** @brief Options parsed from the command line. */
struct CommandLineOptions {
    CliCommand command = CliCommand::Default;            ///< Subcommand, or Default for the flag form.
    bool show_help = false;                              ///< --help / -h / `help`
    std::string help_topic;                              ///< `help CMD` / `CMD --help` topic; empty = general
    bool show_version = false;                           ///< --version / `version`
    bool list_tests = false;                             ///< --list / `list`: print selected test names, run nothing
    ReportFormat report_format = ReportFormat::Console;  ///< --format / --reporter console|json|junit, or --json
    std::string json_file;                               ///< --json-file PATH: also write a JSON report there
    std::string junit_file;                              ///< --junit-file PATH: also write a JUnit XML report there
    std::string name_filter;                             ///< --filter TEXT: run tests whose name contains TEXT
    std::vector<std::string> script_paths;               ///< Positional test scripts (Lua/JS engines: #9, #11)
    std::vector<std::string> include_tags;               ///< --tag TAG (repeatable): script tests with any of these
    std::vector<std::string> exclude_tags;               ///< --exclude-tag TAG (repeatable): never these tags
    std::uint64_t default_timeout_ms = 0;                ///< --timeout MS: per-test default for scripts; 0 = none
    bool is_offline = false;             ///< -offline / --offline: `version` does not look up the latest release (#75)
    bool is_update_check_only = false;   ///< -update -check / --check: report only (#75)
    bool is_update_assumed_yes = false;  ///< -update -yes / --yes: install without asking (#75)
    bool is_update_dry_run = false;      ///< -update -dry-run / --dry-run: download and verify only (#75)
    std::string suite_config_path;       ///< test --config FILE; "" = prk.tests.json in the current folder (#82)
    std::string suite_build_dir;         ///< test --build-dir DIR: the ${build_dir} variable (#82)
    std::vector<std::string> suite_variables;  ///< test --var NAME=VALUE (repeatable) (#82)
};

/** @brief Result of parsing: either options or a usage error message. */
struct CommandLineParseResult {
    CommandLineOptions options;  ///< Valid only when error_message is empty.
    std::string error_message;   ///< Non-empty when the command line is invalid.
};

/**
 * @brief Parses arguments (excluding the program name). Pure function.
 *
 * When the first argument is `run`, `list`, `version` (or `-version`, `-v`), `help`, or `update` (or `-update`,
 * `--update`), the subcommand form is used.
 * Otherwise the original flag form is used, so existing `proofrunkit` command lines keep working.
 * To run a script literally named like a command, use a path such as `.\run`.
 *
 * @param arguments Arguments in order, e.g. {"run", "--filter", "core.", "suite.lua"}.
 * @return Parsed options, or a non-empty error_message describing the first problem found.
 */
[[nodiscard]] CommandLineParseResult parse_command_line(std::span<const std::string_view> arguments);

/**
 * @brief Builds the general help text. Pure function.
 * @param program_name Name shown in the usage lines (`prk` or `proofrunkit`).
 * @return Multi-line usage text ending in a newline.
 */
[[nodiscard]] std::string format_usage_text(std::string_view program_name);

/**
 * @brief Builds the help text for one subcommand. Pure function.
 * @param program_name Name shown in the usage line.
 * @param command_name `run`, `list`, `version`, or `help`.
 * @return Multi-line help text ending in a newline, or an empty string when @p command_name is not a command.
 */
[[nodiscard]] std::string format_command_help_text(std::string_view program_name, std::string_view command_name);

/**
 * @brief Chooses the program name shown in help and errors from argv[0]. Pure function.
 *
 * The same binary is installed as `proofrunkit.exe` and `prk.exe` (#60); this keeps messages
 * consistent with the name the user typed.
 *
 * @param invoked_path argv[0], e.g. `C:\Program Files\ProofRunKit\prk.exe`; may be empty.
 * @return "prk" when the file stem is `prk` (any case), otherwise "proofrunkit".
 */
[[nodiscard]] std::string_view select_program_name(std::string_view invoked_path);

}  // namespace prk
