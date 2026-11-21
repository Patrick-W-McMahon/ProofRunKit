/**
 * @file test_command.cpp
 * @brief Implementation of `prk test` (#82): configuration, variables, run, and the combined report.
 */
#include "test_command.hpp"

#include <memory>

namespace prk {

namespace {

/** @brief State of one replayed result: the result and the shared replay clock. */
struct ReplayedResult {
    const SuiteTestResult* result;  ///< Result to report.
    std::uint64_t* clock_ms;        ///< Replay clock, advanced by the result's elapsed time.
};

/** @brief Test body that reports a recorded result. */
void replay_suite_result(PrkTestContext* context, void* user_state) {
    const ReplayedResult* const replayed = static_cast<const ReplayedResult*>(user_state);
    *replayed->clock_ms += replayed->result->elapsed_ms;
    if (replayed->result->outcome == PRK_OUTCOME_FAILED) {
        prk_fail_test(context, replayed->result->message.c_str());
    } else if (replayed->result->outcome == PRK_OUTCOME_SKIPPED) {
        prk_skip_test(context, replayed->result->message.c_str());
    }
}

/** @brief Reads the replay clock (milliseconds). */
std::uint64_t read_replay_clock(void* clock_state) {
    return *static_cast<const std::uint64_t*>(clock_state);
}

/** @brief Reports @p results through the normal reporters, one replayed test case each. */
int report_suite_results(std::string_view program_name, const CommandLineOptions& options,
                         const std::vector<SuiteTestResult>& results, const CommandLineEnvironment& environment) {
    const std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)> registry(prk_create_registry(),
                                                                                 &prk_destroy_registry);
    std::uint64_t clock_ms = 0;
    std::vector<ReplayedResult> replayed;
    replayed.reserve(results.size());
    for (const SuiteTestResult& result : results) {
        replayed.push_back(ReplayedResult{&result, &clock_ms});
        const PrkTestCase test_case{result.name.c_str(), &replay_suite_result, &replayed.back()};
        if (!registry || prk_register_test_case(registry.get(), &test_case) != PRK_STATUS_OK) {
            environment.errors << program_name << " test: cannot report result '" << result.name << "'\n";
            return kExitScriptError;
        }
    }
    CommandLineOptions report_options = options;
    report_options.name_filter.clear();  // Already applied to entries.
    ScriptHostServices services = environment.script_services;
    services.read_monotonic_milliseconds = [&clock_ms] { return clock_ms; };
    const CommandLineEnvironment replay_environment{environment.builtin_registry,
                                                    PrkClock{&read_replay_clock, &clock_ms},
                                                    environment.read_script_file,
                                                    services,
                                                    environment.write_report_file,
                                                    environment.output,
                                                    environment.errors,
                                                    environment.update_services,
                                                    environment.test_suite_services};
    return run_selected_tests(program_name, report_options, *registry, replay_environment);
}

/** @brief Returns the folder part of @p path ("" when there is none). */
std::string find_parent_folder(const std::string& path) {
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

/** @brief Built-in variables: prk, prk_dir, config_dir, build_dir, work_dir, exe, os. */
TestVariables make_builtin_variables(const CommandLineOptions& options, const std::string& config_path,
                                     const TestSuiteServices& services) {
    TestVariables builtins;
    builtins["prk"] = services.find_own_executable ? services.find_own_executable() : std::string("proofrunkit");
    builtins["prk_dir"] = find_parent_folder(builtins["prk"]);
    builtins["config_dir"] = find_parent_folder(config_path);
    builtins["exe"] = services.executable_suffix;
    builtins["os"] = services.os_name;
    if (!options.suite_build_dir.empty()) {
        builtins["build_dir"] = services.make_absolute_path(options.suite_build_dir);
    }
    const std::string base = options.suite_build_dir.empty() ? builtins["config_dir"] : builtins["build_dir"];
    builtins["work_dir"] =
        base + services.path_separator + (options.suite_build_dir.empty() ? ".prk-test" : "prk-test");
    return builtins;
}

/** @brief Parses --var values into overrides. */
TestVariables make_variable_overrides(const CommandLineOptions& options) {
    TestVariables overrides;
    for (const std::string& setting : options.suite_variables) {
        const std::size_t equals = setting.find('=');
        overrides[setting.substr(0, equals)] = setting.substr(equals + 1);
    }
    return overrides;
}

/** @brief Writes the entry names for `prk test --list`. */
void write_suite_entry_names(const TestConfig& config, const std::string& name_filter, std::ostream& output) {
    for (const TestEntry& step : config.setup) {
        output << "setup/" << step.name << '\n';
    }
    for (const TestEntry& entry : config.tests) {
        if (name_filter.empty() || entry.name.find(name_filter) != std::string::npos) {
            output << entry.name << '\n';
        }
    }
}

/** @brief Resolves variables and folders for a run of @p config. */
SuiteRunSettings make_suite_settings(const CommandLineOptions& options, const TestConfig& config,
                                     const std::string& config_path, const TestSuiteServices& services) {
    TestVariables builtins = make_builtin_variables(options, config_path, services);
    const TestVariables overrides = make_variable_overrides(options);
    if (!config.work_dir.empty() && overrides.find("work_dir") == overrides.end()) {
        std::string missing;
        if (const std::optional<std::string> work_dir = expand_test_variables(config.work_dir, builtins, missing)) {
            builtins["work_dir"] = join_suite_path(builtins["config_dir"], *work_dir, services.path_separator);
        }
    }
    SuiteRunSettings settings;
    settings.variables = resolve_test_variables(config, builtins, overrides, services.path_exists);
    settings.config_dir = settings.variables["config_dir"];
    settings.work_dir = settings.variables["work_dir"];
    settings.name_filter = options.name_filter;
    return settings;
}

}  // namespace

int run_test_command(std::string_view program_name, const CommandLineOptions& options,
                     const CommandLineEnvironment& environment) {
    const TestSuiteServices* const services = environment.test_suite_services;
    if (services == nullptr || !services->read_file || !services->make_absolute_path) {
        environment.errors << program_name << " test: not available in this build\n";
        return kExitUsageError;
    }
    const std::string requested = options.suite_config_path.empty() ? "prk.tests.json" : options.suite_config_path;
    const std::string config_path = services->make_absolute_path(requested);
    const std::optional<std::string> text = services->read_file(config_path);
    if (!text) {
        environment.errors << program_name << " test: cannot read " << config_path
                           << " (run from the project folder or use --config FILE)\n";
        return kExitUsageError;
    }
    std::string error;
    const std::optional<TestConfig> config = parse_test_config(*text, error);
    if (!config) {
        environment.errors << program_name << " test: " << config_path << ": " << error << '\n';
        return kExitScriptError;
    }
    if (options.list_tests) {
        write_suite_entry_names(*config, options.name_filter, environment.output);
        return kExitAllPassed;
    }
    const SuiteRunSettings settings = make_suite_settings(options, *config, config_path, *services);
    std::ostream& progress = options.report_format == ReportFormat::Console ? environment.output : environment.errors;
    progress << program_name << " test: " << config_path << '\n';
    const std::vector<SuiteTestResult> results = run_test_suite(*config, settings, *services, progress);
    progress << '\n';
    return report_suite_results(program_name, options, results, environment);
}

}  // namespace prk
