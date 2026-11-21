/**
 * @file test_suite_runner.cpp
 * @brief Internal tests for running a prk.tests.json (#82) with a fake process runner and in-memory files:
 *        exit-code and output checks, timeouts, and failed starts.
 *        Cases that prk scripts cover are in prk.tests.json (docs/self-hosting.md, #96).
 */
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
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
    services.path_exists = [&world](const std::string& path) {
        return world.files.count(path) > 0 || path == "/b" || path == "/p/x";
    };
    services.find_own_executable = [] { return std::string("/p/prk"); };
    services.make_absolute_path = [](const std::string& path) {
        return path.starts_with("/") ? path : "/repo/" + path;
    };
    return services;
}

SuiteRunSettings make_settings() {
    SuiteRunSettings settings;
    settings.variables = {{"prk", "/p/prk"}, {"greeting", "hi"}, {"bin", "/b"}};
    settings.config_dir = "/repo";
    settings.work_dir = "/w";
    return settings;
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

}  // namespace

PrkStatus register_suite_runner_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"suite.checks_exit_codes_and_output", &test_checks_exit_codes_and_output, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
