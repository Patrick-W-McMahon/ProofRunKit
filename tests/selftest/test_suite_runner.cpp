/**
 * @file test_suite_runner.cpp
 * @brief Tests for running a prk.tests.json (#82) with a fake process runner and in-memory files: command lines,
 *        environment, templates, skips, exit-code and output checks, child JSON reports, setup, and `prk test`.
 */
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cpp/command_line_app.hpp"
#include "cpp/suite/test_command.hpp"
#include "cpp/suite/test_suite.hpp"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

/** @brief What a fake process does. */
struct FakeReply {
    int exit_code = 0;
    std::string output;
    std::string errors;
    PrkProcessOutcome outcome = PRK_PROCESS_EXITED;
};

/** @brief Fake processes and files shared by one test. */
struct FakeWorld {
    std::map<std::string, std::string> files;
    std::vector<std::vector<std::string>> command_lines;
    std::vector<std::string> directories;
    std::vector<std::vector<std::string>> environments;
    std::function<FakeReply(FakeWorld&, const std::vector<std::string>&)> reply;
};

char* copy_to_heap(const std::string& text) {
    char* const copy = static_cast<char*>(std::malloc(text.size() + 1));
    if (copy != nullptr) {
        std::memcpy(copy, text.c_str(), text.size() + 1);
    }
    return copy;
}

PrkStatus spawn_fake_process(void* state, const PrkProcessRequest* request, PrkProcessResult* out_result) {
    FakeWorld& world = *static_cast<FakeWorld*>(state);
    std::vector<std::string> command_line{request->executable};
    command_line.insert(command_line.end(), request->arguments, request->arguments + request->argument_count);
    world.command_lines.push_back(command_line);
    world.directories.emplace_back(request->working_directory);
    world.environments.emplace_back(request->environment, request->environment + request->environment_count);
    const FakeReply reply = world.reply ? world.reply(world, command_line) : FakeReply{};
    *out_result = PrkProcessResult{};
    out_result->outcome = reply.outcome;
    out_result->exit_code = reply.exit_code;
    out_result->standard_output = copy_to_heap(reply.output);
    out_result->standard_output_length = reply.output.size();
    out_result->standard_error = copy_to_heap(reply.errors);
    out_result->standard_error_length = reply.errors.size();
    out_result->elapsed_ms = 7;
    if (reply.outcome == PRK_PROCESS_FAILED_TO_START) {
        std::memcpy(out_result->error_message, "no such program", sizeof "no such program");
    }
    return PRK_STATUS_OK;
}

TestSuiteServices make_fake_services(FakeWorld& world) {
    TestSuiteServices services;
    services.process_runner = PrkProcessRunner{&spawn_fake_process, &world};
    services.read_file = [&world](const std::string& path) -> std::optional<std::string> {
        const auto found = world.files.find(path);
        return found == world.files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    services.write_file = [&world](const std::string& path, const std::string& text) {
        world.files[path] = text;
        return std::string();
    };
    services.make_directories = [&world](const std::string& path) {
        world.files[path + "/"] = "";
        return path.starts_with("/ro") ? std::string("read-only") : std::string();
    };
    services.path_exists = [&world](const std::string& path) { return world.files.count(path) > 0 || path == "/b"; };
    services.find_own_executable = [] { return std::string("/p/prk"); };
    services.make_absolute_path = [](const std::string& path) {
        return path.starts_with("/") ? path : "/repo/" + path;
    };
    return services;
}

/** @brief Writes a two-test JSON report to the path after --json-file. */
FakeReply write_two_test_report(FakeWorld& world, const std::vector<std::string>& command_line) {
    for (std::size_t index = 0; index + 1 < command_line.size(); ++index) {
        if (command_line[index] == "--json-file") {
            world.files[command_line[index + 1]] =
                R"({"tests": [{"name": "a.one", "outcome": "passed", "message": ""},
                              {"name": "a.two", "outcome": "skipped", "message": "later"}]})";
        }
    }
    return FakeReply{};
}

SuiteRunSettings make_settings() {
    SuiteRunSettings settings;
    settings.variables = {{"prk", "/p/prk"}, {"greeting", "hi"}, {"bin", "/b"}};
    settings.config_dir = "/repo";
    settings.work_dir = "/w";
    return settings;
}

TestEntry make_run_entry(const std::string& name, const std::string& script) {
    TestEntry entry;
    entry.name = name;
    entry.scripts = {script};
    return entry;
}

void test_runs_script_entries(PrkTestContext* context, void*) {
    FakeWorld world;
    world.reply = &write_two_test_report;
    TestConfig config;
    config.default_env = {{"PRK_OFFLINE", "1"}, {"A", "default"}};
    TestEntry entry = make_run_entry("e", "t/a.lua");
    entry.arguments = {"--tag", "${greeting}"};
    entry.env = {{"A", "${bin}"}};
    entry.working_directory = "${bin}";
    entry.make_dirs = {"out/${greeting}"};
    const std::vector<SuiteTestResult> results =
        run_test_entry(entry, config, make_settings(), make_fake_services(world));
    PRK_EXPECT(world.command_lines.size() == 1);
    PRK_EXPECT((world.command_lines[0] == std::vector<std::string>{"/p/prk", "run", "/repo/t/a.lua", "--tag", "hi",
                                                                   "--json-file", "/w/results/e.json"}));
    PRK_EXPECT(world.directories[0] == "/b" && world.files.count("/repo/out/hi/") == 1);
    entry.make_dirs = {"/ro/x"};
    PRK_EXPECT(run_test_entry(entry, config, make_settings(), make_fake_services(world))[0].message ==
               "cannot create /ro/x: read-only");
    PRK_EXPECT((world.environments[0] == std::vector<std::string>{"PRK_OFFLINE=1", "A=/b"}));
    PRK_EXPECT(results.size() == 2 && results[0].name == "e/a.one" && results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(results[1].outcome == PRK_OUTCOME_SKIPPED && results[1].message == "later");
}

void test_checks_exit_codes_and_output(PrkTestContext* context, void*) {
    FakeWorld world;
    world.reply = [](FakeWorld&, const std::vector<std::string>&) { return FakeReply{2, "", "prk: unknown option\n"}; };
    const TestSuiteServices services = make_fake_services(world);
    TestEntry entry;
    entry.name = "c";
    entry.command = {"${bin}/tool", "x"};
    std::vector<SuiteTestResult> results = run_test_entry(entry, TestConfig{}, make_settings(), services);
    PRK_EXPECT(world.command_lines.back() == (std::vector<std::string>{"/b/tool", "x"}));
    PRK_EXPECT(results.size() == 1 && results[0].outcome == PRK_OUTCOME_FAILED && results[0].elapsed_ms == 7);
    PRK_EXPECT(results[0].message == "exit code 2, expected 0 | prk: unknown option");
    entry.expected_exit_code = 2;
    entry.expected_output = "unknown \\w+";
    PRK_EXPECT(run_test_entry(entry, TestConfig{}, make_settings(), services)[0].outcome == PRK_OUTCOME_PASSED);
    entry.rejected_output = "option";
    PRK_EXPECT(run_test_entry(entry, TestConfig{}, make_settings(), services)[0].message.starts_with("output matches"));
    entry.rejected_output = "(";
    PRK_EXPECT(run_test_entry(entry, TestConfig{}, make_settings(), services)[0].message.starts_with("invalid"));
    entry.rejected_output.clear();
    entry.expected_output = "missing";
    PRK_EXPECT(run_test_entry(entry, TestConfig{}, make_settings(), services)[0].message.starts_with("output does"));
    world.reply = [](FakeWorld&, const std::vector<std::string>&) {
        return FakeReply{0, "", "", PRK_PROCESS_FAILED_TO_START};
    };
    entry.command = {"tool"};
    PRK_EXPECT(run_test_entry(entry, TestConfig{}, make_settings(), services)[0].message ==
               "failed to start: no such program");
    PRK_EXPECT(world.command_lines.back().front() == "tool");
    world.reply = [](FakeWorld&, const std::vector<std::string>&) {
        return FakeReply{0, "", "", PRK_PROCESS_TIMED_OUT};
    };
    PRK_EXPECT(run_test_entry(entry, TestConfig{}, make_settings(), services)[0].message == "timed out");
}

void test_skips_and_templates(PrkTestContext* context, void*) {
    FakeWorld world;
    world.reply = &write_two_test_report;
    const TestSuiteServices services = make_fake_services(world);
    TestEntry entry = make_run_entry("t", "x/${nope}.lua");
    std::vector<SuiteTestResult> results = run_test_entry(entry, TestConfig{}, make_settings(), services);
    PRK_EXPECT(results.size() == 1 && results[0].outcome == PRK_OUTCOME_SKIPPED &&
               results[0].message == "needs ${nope}");
    entry = make_run_entry("t", "cli.js.in");
    entry.skip_unless = {"${bin}/missing"};
    PRK_EXPECT(run_test_entry(entry, TestConfig{}, make_settings(), services)[0].message == "missing /b/missing");
    PRK_EXPECT(world.command_lines.empty());
    entry.skip_unless.clear();
    entry.is_template = true;
    PRK_EXPECT(run_test_entry(entry, TestConfig{}, make_settings(), services)[0].message ==
               "cannot read template /repo/cli.js.in");
    world.files["/repo/cli.js.in"] = "say('${greeting}', '$${literal}')";
    results = run_test_entry(entry, TestConfig{}, make_settings(), services);
    PRK_EXPECT(world.files.at("/w/templates/t/cli.js") == "say('hi', '${literal}')");
    PRK_EXPECT(world.command_lines.size() == 1 && world.command_lines[0][2] == "/w/templates/t/cli.js");
    PRK_EXPECT(results.size() == 2);
    world.files["/repo/cli.js.in"] = "${unknown}";
    PRK_EXPECT(run_test_entry(entry, TestConfig{}, make_settings(), services)[0].message ==
               "needs ${unknown} (in /repo/cli.js.in)");
}

void test_reads_child_reports(PrkTestContext* context, void*) {
    FakeWorld world;
    const TestSuiteServices services = make_fake_services(world);
    std::string problem;
    PRK_EXPECT(read_suite_entry_report("e", "/r.json", services, problem).empty() &&
               problem == "no JSON report at /r.json");
    world.files["/r.json"] = "{\"tests\": 3}";
    PRK_EXPECT(read_suite_entry_report("e", "/r.json", services, problem).empty() && problem.starts_with("invalid"));
    world.files["/r.json"] = R"({"tests": [{"name": "x", "outcome": "failed", "message": "m"}]})";
    const std::vector<SuiteTestResult> results = read_suite_entry_report("e", "/r.json", services, problem);
    PRK_EXPECT(problem.empty() && results.size() == 1 && results[0].name == "e/x" && results[0].message == "m");
    PRK_EXPECT(results[0].outcome == PRK_OUTCOME_FAILED);
    TestEntry entry = make_run_entry("noreport", "a.lua");
    const std::vector<SuiteTestResult> missing = run_test_entry(entry, TestConfig{}, make_settings(), services);
    PRK_EXPECT(missing.size() == 1 && missing[0].message == "no JSON report at /w/results/noreport.json");
}

void test_runs_setup_then_selected_tests(PrkTestContext* context, void*) {
    FakeWorld world;
    world.reply = [](FakeWorld&, const std::vector<std::string>& command_line) {
        return FakeReply{command_line.back() == "fail" ? 1 : 0, "", ""};
    };
    TestConfig config;
    TestEntry step;
    step.name = "gen";
    step.command = {"gen", "ok"};
    TestEntry first = step;
    first.name = "self.one";
    TestEntry second = step;
    second.name = "other";
    config.setup = {step};
    config.tests = {first, second};
    SuiteRunSettings settings = make_settings();
    settings.name_filter = "self.";
    std::ostringstream progress;
    std::vector<SuiteTestResult> results = run_test_suite(config, settings, make_fake_services(world), progress);
    PRK_EXPECT(results.size() == 1 && results[0].name == "self.one" && world.command_lines.size() == 2);
    PRK_EXPECT(progress.str().find("setup gen ... [ PASS ]") != std::string::npos);
    config.setup[0].command = {"gen", "fail"};
    results = run_test_suite(config, settings, make_fake_services(world), progress);
    PRK_EXPECT(results.size() == 1 && results[0].name == "setup/gen" && results[0].outcome == PRK_OUTCOME_FAILED);
    PRK_EXPECT(results[0].message.starts_with("gen: exit code 1"));
}

int run_fake_test_command(FakeWorld& world, const std::vector<std::string_view>& arguments, std::string& out_text) {
    const TestSuiteServices services = make_fake_services(world);
    PrkRegistry* const builtins = prk_create_registry();
    std::ostringstream output;
    std::ostringstream errors;
    const CommandLineEnvironment environment{*builtins,
                                             PrkClock{nullptr, nullptr},
                                             ScriptFileReader{},
                                             ScriptHostServices{},
                                             ReportFileWriter{},
                                             output,
                                             errors,
                                             nullptr,
                                             &services};
    const int exit_code = run_command_line("prk", arguments, environment);
    prk_destroy_registry(builtins);
    out_text = output.str() + errors.str();
    return exit_code;
}

void test_runs_test_command(PrkTestContext* context, void*) {
    FakeWorld world;
    world.reply = &write_two_test_report;
    std::string text;
    PRK_EXPECT(run_fake_test_command(world, {"test"}, text) == kExitUsageError);
    PRK_EXPECT(text.find("cannot read /repo/prk.tests.json") != std::string::npos);
    world.files["/repo/prk.tests.json"] = R"({"schema_version": 1, "tests": [{"name": "x", "run": "a.lua"}],
                                             "variables": {"out": "${work_dir}"}})";
    PRK_EXPECT(run_fake_test_command(world, {"test", "--list"}, text) == kExitAllPassed && text == "x\n");
    PRK_EXPECT(run_fake_test_command(world, {"test", "--build-dir", "/b"}, text) == kExitAllPassed);
    PRK_EXPECT(world.command_lines.back().back() == "/b/prk-test/results/x.json");
    PRK_EXPECT(text.find("[ PASS ] x/a.one") != std::string::npos &&
               text.find("2 tests: 1 passed, 0 failed, 1 skipped") != std::string::npos);
    PRK_EXPECT(run_fake_test_command(world, {"test", "--var", "work_dir=/v", "--json"}, text) == kExitAllPassed);
    PRK_EXPECT(world.command_lines.back().back() == "/v/results/x.json" &&
               text.find("\"x/a.two\"") != std::string::npos);
    world.files["/repo/c.json"] = "{\"schema_version\": 1, \"tests\": [{\"name\": \"x\", \"command\": \"t\"}]}";
    world.reply = [](FakeWorld&, const std::vector<std::string>&) { return FakeReply{1, "", "boom"}; };
    PRK_EXPECT(run_fake_test_command(world, {"test", "--config", "c.json"}, text) == kExitTestFailed);
    PRK_EXPECT(text.find("[ FAIL ] x") != std::string::npos && text.find("/repo/.prk-test") == std::string::npos);
    world.files["/repo/prk.tests.json"] = "{\"schema_version\": 1, \"tests\": 1}";
    PRK_EXPECT(run_fake_test_command(world, {"test"}, text) == kExitScriptError);
    PRK_EXPECT(text.find("tests: expected a list of entries") != std::string::npos);
}

void test_system_services(PrkTestContext* context, void*) {
    const TestSuiteServices services = make_system_test_suite_services();
    PRK_EXPECT(services.process_runner.spawn_process != nullptr && services.read_file && services.write_file);
    PRK_EXPECT(services.path_exists(".") && !services.path_exists("") && !services.path_exists("no/such/prk/file"));
    PRK_EXPECT(!services.read_file("no/such/prk/file").has_value());
    const std::string absolute = services.make_absolute_path("x");
    PRK_EXPECT(absolute.size() > 2 && absolute.ends_with("/x") && join_suite_path("/q", absolute, '/') == absolute);
    PRK_EXPECT(absolute.find('\\') == std::string::npos && services.make_directories(".").empty());
    PRK_EXPECT(services.path_exists(services.find_own_executable()));
}

}  // namespace

PrkStatus register_suite_runner_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"suite.runs_script_entries", &test_runs_script_entries, nullptr},
        {"suite.checks_exit_codes_and_output", &test_checks_exit_codes_and_output, nullptr},
        {"suite.skips_and_templates", &test_skips_and_templates, nullptr},
        {"suite.reads_child_reports", &test_reads_child_reports, nullptr},
        {"suite.runs_setup_then_selected_tests", &test_runs_setup_then_selected_tests, nullptr},
        {"suite.runs_test_command", &test_runs_test_command, nullptr},
        {"suite.system_services", &test_system_services, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
