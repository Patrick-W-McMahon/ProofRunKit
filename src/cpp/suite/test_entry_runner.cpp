/**
 * @file test_entry_runner.cpp
 * @brief Runs one `prk.tests.json` entry (#82): expands its variables, writes templates, starts it through the
 *        injected process runner, and checks its exit code and output.
 */
#include <algorithm>
#include <cctype>
#include <regex>

#include "test_suite.hpp"

namespace prk {

namespace {

/** @brief Expands strings of one entry and remembers the first missing variable. */
struct EntryExpander {
    const TestVariables& variables;  ///< Resolved variables.
    std::string missing;             ///< First missing variable, or "".

    /** @brief Expands @p text; returns "" and records the variable when one is missing. */
    std::string expand(const std::string& text) {
        std::string name;
        std::optional<std::string> value = expand_test_variables(text, variables, name);
        if (!value && missing.empty()) {
            missing = name;
        }
        return value ? *value : std::string();
    }
};

/** @brief Command line, folder, and environment of one entry, ready to start. */
struct PreparedEntry {
    std::vector<std::string> command_line;  ///< Program first.
    std::string working_directory;          ///< Absolute or "" (config folder).
    std::vector<std::string> environment;   ///< NAME=VALUE.
    std::string report_path;                ///< Child JSON report, or "" when not read.
    std::vector<std::string> directories;   ///< Folders to create first (make_dirs).
    std::string skip_reason;                ///< Non-empty: do not run; skip with this reason.
};

/** @brief Returns a short excerpt for failure messages: the start of stderr (where errors are), else the end of
 *         stdout (where summaries are), on one line. */
std::string describe_output_excerpt(const PrkProcessResult& result, std::size_t limit) {
    std::string excerpt;
    if (result.standard_error_length > 0) {
        excerpt.assign(result.standard_error, std::min(result.standard_error_length, limit));
    } else {
        const std::size_t length = result.standard_output_length;
        const std::size_t start = length > limit ? length - limit : 0;
        excerpt.assign(result.standard_output + start, length - start);
    }
    for (char& character : excerpt) {
        character = (character == '\n' || character == '\r' || character == '\t') ? ' ' : character;
    }
    excerpt.erase(
        std::unique(excerpt.begin(), excerpt.end(), [](char left, char right) { return left == ' ' && right == ' '; }),
        excerpt.end());
    while (!excerpt.empty() && excerpt.back() == ' ') {
        excerpt.pop_back();
    }
    return excerpt.empty() ? std::string() : " | " + excerpt;
}

/** @brief Expands and writes one template script; returns its new path, or "" with @p out_problem set. */
std::string write_template_script(const std::string& script_path, const TestEntry& entry,
                                  const SuiteRunSettings& settings, const TestSuiteServices& services,
                                  std::string& out_problem) {
    const std::optional<std::string> text = services.read_file ? services.read_file(script_path) : std::nullopt;
    if (!text) {
        out_problem = "cannot read template " + script_path;
        return {};
    }
    std::string missing;
    const std::optional<std::string> expanded = expand_test_variables(*text, settings.variables, missing);
    if (!expanded) {
        out_problem = "needs ${" + missing + "} (in " + script_path + ")";
        return {};
    }
    std::string file_name = script_path.substr(script_path.find_last_of("/\\") + 1);
    if (file_name.size() > 3 && file_name.ends_with(".in")) {
        file_name.resize(file_name.size() - 3);
    }
    const char separator = services.path_separator;
    const std::string path =
        settings.work_dir + separator + "templates" + separator + entry.name + separator + file_name;
    const std::string problem = services.write_file ? services.write_file(path, *expanded) : "no file access";
    if (!problem.empty()) {
        out_problem = "cannot write " + path + ": " + problem;
        return {};
    }
    return path;
}

/** @brief Builds the environment: defaults, then the entry's own settings (same name replaces). */
std::vector<std::string> build_entry_environment(const TestEntry& entry, const TestConfig& config,
                                                 EntryExpander& expander) {
    std::vector<EnvironmentSetting> merged = config.default_env;
    for (const EnvironmentSetting& setting : entry.env) {
        std::erase_if(merged, [&setting](const EnvironmentSetting& other) { return other.first == setting.first; });
        merged.push_back(setting);
    }
    std::vector<std::string> environment;
    for (const auto& [name, value] : merged) {
        environment.push_back(name + "=" + expander.expand(value));
    }
    return environment;
}

/** @brief Adds the scripts (or the command) and the arguments to @p prepared. */
void add_entry_command_line(const TestEntry& entry, const TestConfig& config, const SuiteRunSettings& settings,
                            const TestSuiteServices& services, EntryExpander& expander, PreparedEntry& prepared) {
    const char separator = services.path_separator;
    if (!entry.scripts.empty()) {
        prepared.command_line = {expander.expand(config.runner), "run"};
        for (const std::string& script : entry.scripts) {
            std::string path = join_suite_path(settings.config_dir, expander.expand(script), separator);
            if (entry.is_template && expander.missing.empty() && prepared.skip_reason.empty()) {
                path = write_template_script(path, entry, settings, services, prepared.skip_reason);
            }
            prepared.command_line.push_back(path);
        }
    } else {
        const std::string program = expander.expand(entry.command.front());
        const bool is_bare_name = program.find_first_of("/\\") == std::string::npos;
        prepared.command_line = {is_bare_name ? program : join_suite_path(settings.config_dir, program, separator)};
        for (std::size_t index = 1; index < entry.command.size(); ++index) {
            prepared.command_line.push_back(expander.expand(entry.command[index]));
        }
    }
    for (const std::string& argument : entry.arguments) {
        prepared.command_line.push_back(expander.expand(argument));
    }
}

/** @brief Expands everything an entry needs; sets skip_reason when it cannot or should not run. */
PreparedEntry prepare_entry(const TestEntry& entry, const TestConfig& config, const SuiteRunSettings& settings,
                            const TestSuiteServices& services) {
    PreparedEntry prepared;
    EntryExpander expander{settings.variables, {}};
    const char separator = services.path_separator;
    for (const std::string& required : entry.skip_unless) {
        const std::string path = join_suite_path(settings.config_dir, expander.expand(required), separator);
        if (expander.missing.empty() && prepared.skip_reason.empty() &&
            !(services.path_exists && services.path_exists(path))) {
            prepared.skip_reason = "missing " + path;
        }
    }
    for (const std::string& folder : entry.make_dirs) {
        prepared.directories.push_back(join_suite_path(settings.config_dir, expander.expand(folder), separator));
    }
    add_entry_command_line(entry, config, settings, services, expander, prepared);
    prepared.environment = build_entry_environment(entry, config, expander);
    const std::string cwd = entry.working_directory.empty() ? std::string() : expander.expand(entry.working_directory);
    prepared.working_directory =
        cwd.empty() ? settings.config_dir : join_suite_path(settings.config_dir, cwd, separator);
    if (!expander.missing.empty()) {
        prepared.skip_reason = "needs ${" + expander.missing + "}";
    }
    const bool is_report_read = entry.is_report_read.value_or(!entry.scripts.empty() && entry.expected_exit_code == 0);
    if (is_report_read && prepared.skip_reason.empty()) {
        prepared.report_path = settings.work_dir + separator + "results" + separator + entry.name + ".json";
        prepared.command_line.push_back("--json-file");
        prepared.command_line.push_back(prepared.report_path);
    }
    return prepared;
}

/** @brief Checks exit code and output; returns "" or why the entry failed. */
std::string check_entry_result(const TestEntry& entry, const PrkProcessResult& result) {
    if (result.outcome == PRK_PROCESS_FAILED_TO_START) {
        return std::string("failed to start: ") + result.error_message;
    }
    if (result.outcome != PRK_PROCESS_EXITED) {
        return result.outcome == PRK_PROCESS_TIMED_OUT ? "timed out" : "ended by a signal";
    }
    if (result.exit_code != entry.expected_exit_code) {
        return "exit code " + std::to_string(result.exit_code) + ", expected " +
               std::to_string(entry.expected_exit_code) + describe_output_excerpt(result, 240);
    }
    const std::string output = std::string(result.standard_output, result.standard_output_length) +
                               std::string(result.standard_error, result.standard_error_length);
    try {
        if (!entry.expected_output.empty() && !std::regex_search(output, std::regex(entry.expected_output))) {
            return "output does not match expect_output /" + entry.expected_output + "/" +
                   describe_output_excerpt(result, 200);
        }
        if (!entry.rejected_output.empty() && std::regex_search(output, std::regex(entry.rejected_output))) {
            return "output matches reject_output /" + entry.rejected_output + "/";
        }
    } catch (const std::regex_error&) {
        return "invalid expect_output or reject_output regular expression";
    }
    return {};
}

/** @brief Starts @p prepared and fills @p out_result; returns "" or why it could not start. */
std::string start_entry(const PreparedEntry& prepared, std::uint64_t timeout_ms, const TestSuiteServices& services,
                        PrkProcessResult& out_result) {
    std::vector<const char*> arguments;
    for (std::size_t index = 1; index < prepared.command_line.size(); ++index) {
        arguments.push_back(prepared.command_line[index].c_str());
    }
    std::vector<const char*> environment;
    for (const std::string& setting : prepared.environment) {
        environment.push_back(setting.c_str());
    }
    const PrkProcessRequest request{prepared.command_line.front().c_str(),
                                    arguments.data(),
                                    arguments.size(),
                                    prepared.working_directory.c_str(),
                                    environment.data(),
                                    environment.size(),
                                    1,
                                    nullptr,
                                    0,
                                    timeout_ms,
                                    4U * 1024U * 1024U};
    if (services.process_runner.spawn_process == nullptr) {
        return "process spawning is not available";
    }
    const PrkStatus status =
        services.process_runner.spawn_process(services.process_runner.runner_state, &request, &out_result);
    return status == PRK_STATUS_OK ? std::string() : std::string("cannot start: ") + prk_get_status_name(status);
}

}  // namespace

std::string join_suite_path(const std::string& base, const std::string& path, char separator) {
    const bool is_absolute = (!path.empty() && (path[0] == '/' || path[0] == '\\')) ||
                             (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':');
    if (is_absolute || base.empty()) {
        return path;
    }
    if (path.empty() || path == ".") {
        return base;
    }
    const bool has_separator = base.back() == '/' || base.back() == '\\';
    return has_separator ? base + path : base + separator + path;
}

std::vector<SuiteTestResult> run_test_entry(const TestEntry& entry, const TestConfig& config,
                                            const SuiteRunSettings& settings, const TestSuiteServices& services) {
    const PreparedEntry prepared = prepare_entry(entry, config, settings, services);
    if (!prepared.skip_reason.empty()) {
        return {SuiteTestResult{entry.name, PRK_OUTCOME_SKIPPED, prepared.skip_reason, 0}};
    }
    for (const std::string& folder : prepared.directories) {
        const std::string problem = services.make_directories ? services.make_directories(folder) : "no file access";
        if (!problem.empty()) {
            return {SuiteTestResult{entry.name, PRK_OUTCOME_FAILED, "cannot create " + folder + ": " + problem, 0}};
        }
    }
    if (!prepared.report_path.empty() && services.write_file) {
        (void)services.write_file(prepared.report_path, "");  // No stale report from an earlier run.
    }
    PrkProcessResult result{};
    const std::uint64_t timeout_ms = entry.timeout_ms != 0 ? entry.timeout_ms : config.default_timeout_ms;
    std::string problem = start_entry(prepared, timeout_ms, services, result);
    if (!problem.empty()) {
        return {SuiteTestResult{entry.name, PRK_OUTCOME_FAILED, problem, 0}};
    }
    problem = check_entry_result(entry, result);
    const std::uint64_t elapsed_ms = result.elapsed_ms;
    prk_release_process_result(&result);
    std::vector<SuiteTestResult> results;
    if (!prepared.report_path.empty()) {
        std::string report_problem;
        results = read_suite_entry_report(entry.name, prepared.report_path, services, report_problem);
        if (problem.empty()) {
            problem = report_problem;
        }
    }
    if (!problem.empty() || results.empty()) {
        results.push_back(SuiteTestResult{entry.name, problem.empty() ? PRK_OUTCOME_PASSED : PRK_OUTCOME_FAILED,
                                          problem, elapsed_ms});
    }
    return results;
}

}  // namespace prk
