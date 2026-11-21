/**
 * @file command_line.cpp
 * @brief Implementation of the pure command-line parser and help text (#7, #60).
 */
#include "command_line.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <utility>

namespace prk {

namespace {

/** @brief Which flags a parsing context accepts. */
struct FlagRules {
    bool allows_global_flags;  ///< --version and --list (flag form only).
};

/** @brief Parses a --format / --reporter value; returns std::nullopt when the value is not recognised. */
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

/** @brief Maps a subcommand word to its CliCommand; std::nullopt when the word is not a command. */
std::optional<CliCommand> find_command(std::string_view word) {
    if (word == "run") {
        return CliCommand::Run;
    }
    if (word == "list") {
        return CliCommand::List;
    }
    if (word == "version") {
        return CliCommand::Version;
    }
    if (word == "help") {
        return CliCommand::Help;
    }
    return std::nullopt;
}

/** @brief Returns true when @p argument is an option that takes a value. */
bool is_option_with_value(std::string_view argument) {
    return argument == "--filter" || argument == "--format" || argument == "--reporter" || argument == "--tag" ||
           argument == "--exclude-tag" || argument == "--timeout";
}

/** @brief Parses a --timeout value: a positive whole number of milliseconds up to 24 hours. */
std::optional<std::uint64_t> parse_timeout_milliseconds(std::string_view value) {
    constexpr std::uint64_t kMaximumTimeoutMs = 24ULL * 60 * 60 * 1000;
    if (value.empty() || value.size() > 8) {
        return std::nullopt;
    }
    std::uint64_t milliseconds = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        milliseconds = milliseconds * 10 + static_cast<std::uint64_t>(character - '0');
    }
    if (milliseconds == 0 || milliseconds > kMaximumTimeoutMs) {
        return std::nullopt;
    }
    return milliseconds;
}

/**
 * @brief Applies one option that takes a value to @p options.
 * @return An empty string, or the usage error.
 */
std::string apply_option_value(std::string_view option, std::string_view value, CommandLineOptions& options) {
    if (option == "--filter") {
        options.name_filter = std::string(value);
    } else if (option == "--tag" || option == "--exclude-tag") {
        if (value.empty()) {
            return std::string(option) + " needs a non-empty tag";
        }
        (option == "--tag" ? options.include_tags : options.exclude_tags).emplace_back(value);
    } else if (option == "--timeout") {
        const std::optional<std::uint64_t> timeout = parse_timeout_milliseconds(value);
        if (!timeout) {
            return "invalid --timeout '" + std::string(value) + "' (use milliseconds, 1 to 86400000)";
        }
        options.default_timeout_ms = *timeout;
    } else if (const std::optional<ReportFormat> format = parse_report_format(value)) {
        options.report_format = *format;
    } else {
        return "unknown " + std::string(option) + " '" + std::string(value) + "' (use console or json)";
    }
    return {};
}

/**
 * @brief Parses option and script arguments into a copy of @p initial_options.
 * @param arguments Arguments after any subcommand word.
 * @param initial_options Options already implied by the subcommand.
 * @param rules Flags allowed in this context.
 * @return Parsed options or the first error.
 */
CommandLineParseResult parse_option_arguments(std::span<const std::string_view> arguments,
                                              CommandLineOptions initial_options, const FlagRules& rules) {
    CommandLineOptions options = std::move(initial_options);
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if (argument == "--json") {
            options.report_format = ReportFormat::Json;
        } else if (rules.allows_global_flags && argument == "--version") {
            options.show_version = true;
        } else if (rules.allows_global_flags && argument == "--list") {
            options.list_tests = true;
        } else if (is_option_with_value(argument)) {
            if (index + 1 >= arguments.size()) {
                return make_parse_error(std::string(argument) + " needs a value");
            }
            std::string problem = apply_option_value(argument, arguments[++index], options);
            if (!problem.empty()) {
                return make_parse_error(std::move(problem));
            }
        } else if (argument.starts_with("-")) {
            return make_parse_error("unknown option '" + std::string(argument) + "'");
        } else {
            options.script_paths.emplace_back(argument);
        }
    }
    return CommandLineParseResult{std::move(options), {}};
}

/** @brief Parses `help [command]`. */
CommandLineParseResult parse_help_command(std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    options.command = CliCommand::Help;
    options.show_help = true;
    if (arguments.size() > 1) {
        return make_parse_error("help takes at most one command name");
    }
    if (arguments.size() == 1) {
        if (!find_command(arguments[0])) {
            return make_parse_error("unknown command '" + std::string(arguments[0]) + "' (see help)");
        }
        options.help_topic = std::string(arguments[0]);
    }
    return CommandLineParseResult{std::move(options), {}};
}

/** @brief Parses `version`, which accepts only --help. */
CommandLineParseResult parse_version_command(std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    options.command = CliCommand::Version;
    options.show_version = true;
    for (const std::string_view argument : arguments) {
        if (argument != "--help" && argument != "-h") {
            return make_parse_error("version takes no arguments (got '" + std::string(argument) + "')");
        }
        options.show_help = true;
        options.help_topic = "version";
    }
    return CommandLineParseResult{std::move(options), {}};
}

/** @brief Parses `run ...` or `list ...`; `--help` inside them selects that command's help. */
CommandLineParseResult parse_test_command(CliCommand command, std::string_view command_name,
                                          std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    options.command = command;
    options.list_tests = command == CliCommand::List;
    CommandLineParseResult parsed = parse_option_arguments(arguments, std::move(options), FlagRules{false});
    if (parsed.error_message.empty() && parsed.options.show_help) {
        parsed.options.help_topic = std::string(command_name);
    }
    return parsed;
}

/** @brief Returns @p text lower-cased (ASCII only). */
std::string make_ascii_lowercase(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered;
}

}  // namespace

CommandLineParseResult parse_command_line(std::span<const std::string_view> arguments) {
    if (!arguments.empty()) {
        if (const std::optional<CliCommand> command = find_command(arguments[0])) {
            const std::span<const std::string_view> rest = arguments.subspan(1);
            switch (*command) {
                case CliCommand::Help:
                    return parse_help_command(rest);
                case CliCommand::Version:
                    return parse_version_command(rest);
                case CliCommand::Run:
                case CliCommand::List:
                    return parse_test_command(*command, arguments[0], rest);
                case CliCommand::Default:
                    break;
            }
        }
    }
    return parse_option_arguments(arguments, CommandLineOptions{}, FlagRules{true});
}

std::string format_usage_text(std::string_view program_name) {
    const std::string name(program_name);
    return "Usage: " + name + " [options] [test scripts...]\n" +  //
           "       " + name + " <command> [arguments]\n" +        //
           "\n"
           "Commands:\n"
           "  run [scripts...]   Run tests (the built-in checks when no scripts are given).\n"
           "  list [scripts...]  List the selected test names without running them.\n"
           "  version            Show the version and the cycle-counter implementation.\n"
           "  help [command]     Show this help, or help for one command.\n"
           "\n"
           "Options:\n"
           "  -h, --help             Show help and exit.\n"
           "      --version          Show the version and the cycle-counter implementation, then exit.\n"
           "      --list             List the selected test names without running them.\n"
           "      --filter TEXT      Run only tests whose name contains TEXT.\n"
           "      --format FORMAT    Result format: console (default) or json.\n"
           "      --reporter FORMAT  Same as --format.\n"
           "      --json             Same as --format json.\n"
           "      --tag TAG          Script tests: select tests carrying TAG (repeatable).\n"
           "      --exclude-tag TAG  Script tests: leave out tests carrying TAG (repeatable).\n"
           "      --timeout MS       Script tests: default per-test timeout in milliseconds.\n"
           "\n"
           "Exit codes: 0 all selected tests passed, 1 a test failed, 2 usage error,\n"
           "            3 requested feature not implemented yet (e.g. JavaScript test scripts),\n"
           "            4 a test script could not be read, compiled, or loaded.\n";
}

std::string format_command_help_text(std::string_view program_name, std::string_view command_name) {
    const std::string name(program_name);
    const std::string test_options =
        "\n"
        "Options:\n"
        "      --filter TEXT      Select only tests whose name contains TEXT.\n"
        "      --reporter FORMAT  Result format: console (default) or json. --format is an alias.\n"
        "      --json             Same as --reporter json.\n"
        "      --tag TAG          Select only script tests carrying TAG (repeatable; any tag matches).\n"
        "      --exclude-tag TAG  Leave out script tests carrying TAG (repeatable; wins over --tag).\n"
        "      --timeout MS       Default per-test timeout for script tests, in milliseconds.\n"
        "  -h, --help             Show this help.\n";
    if (command_name == "run") {
        return "Usage: " + name + " run [options] [test scripts...]\n\n" +
               "Runs the tests registered by the given Lua scripts (.lua), or the built-in checks\n"
               "when no scripts are given, and reports the results.\n" +
               test_options;
    }
    if (command_name == "list") {
        return "Usage: " + name + " list [options] [test scripts...]\n\n" +
               "Prints the names of the selected tests, one per line, without running them.\n" + test_options;
    }
    if (command_name == "version") {
        return "Usage: " + name + " version\n\n" +
               "Prints the ProofRunKit version and the cycle-counter implementation.\n";
    }
    if (command_name == "help") {
        return "Usage: " + name + " help [command]\n\n" +
               "Shows general help, or help for run, list, version, or help.\n";
    }
    return {};
}

std::string_view select_program_name(std::string_view invoked_path) {
    const std::size_t separator = invoked_path.find_last_of("/\\");
    std::string_view stem = separator == std::string_view::npos ? invoked_path : invoked_path.substr(separator + 1);
    if (stem.size() >= 4 && make_ascii_lowercase(stem.substr(stem.size() - 4)) == ".exe") {
        stem = stem.substr(0, stem.size() - 4);
    }
    return make_ascii_lowercase(stem) == "prk" ? std::string_view("prk") : std::string_view("proofrunkit");
}

}  // namespace prk
