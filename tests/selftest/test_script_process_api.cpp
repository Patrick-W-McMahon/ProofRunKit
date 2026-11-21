/**
 * @file test_script_process_api.cpp
 * @brief Tests for process spawn from test scripts (#12 over #13): prk.run_process (Lua) and
 *        prk.runProcess (JavaScript) with a fake runner that records requests, prk.expect_exit_code /
 *        prk.expectExitCode messages, the timeout cap, and one real spawn of prk_spawn_child.
 */
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "lua_script_fixture.hpp"
#include "proofrunkit/prk_js.h"

namespace prk::selftest {

namespace {

/** @brief A fake PrkProcessRunner: records the first request and returns a canned result every time. */
struct FakeProcessRunner {
    int spawn_count = 0;
    std::string program;
    std::vector<std::string> arguments;
    std::string working_directory;
    std::vector<std::string> environment;
    int is_environment_inherited = -1;
    std::string standard_input;
    std::uint64_t timeout_ms = 0;
    std::size_t output_limit_bytes = 0;
    PrkProcessOutcome outcome = PRK_PROCESS_EXITED;
    int exit_code = 0;
    std::string standard_output;
    std::string standard_error;
    std::string error_message;

    [[nodiscard]] PrkProcessRunner make_runner() { return PrkProcessRunner{&FakeProcessRunner::spawn, this}; }

private:
    void record_request(const PrkProcessRequest& request) {
        program = request.executable;
        arguments.assign(request.arguments, request.arguments + request.argument_count);
        working_directory = request.working_directory != nullptr ? request.working_directory : "";
        environment.assign(request.environment, request.environment + request.environment_count);
        is_environment_inherited = request.is_environment_inherited;
        standard_input.assign(request.standard_input != nullptr ? request.standard_input : "",
                              request.standard_input_length);
        timeout_ms = request.timeout_ms;
        output_limit_bytes = request.output_limit_bytes;
    }

    static char* copy_bytes(const std::string& text) {
        char* const bytes = static_cast<char*>(std::malloc(text.size() + 1));
        if (bytes != nullptr) {
            std::memcpy(bytes, text.c_str(), text.size() + 1);
        }
        return bytes;
    }

    static PrkStatus spawn(void* state, const PrkProcessRequest* request, PrkProcessResult* out_result) {
        FakeProcessRunner& fake = *static_cast<FakeProcessRunner*>(state);
        fake.spawn_count += 1;
        if (fake.spawn_count == 1) {
            fake.record_request(*request);
        }
        std::memset(out_result, 0, sizeof *out_result);
        out_result->outcome = fake.outcome;
        out_result->exit_code = fake.exit_code;
        out_result->elapsed_ms = 12;
        out_result->standard_output = copy_bytes(fake.standard_output);
        out_result->standard_output_length = fake.standard_output.size();
        out_result->standard_error = copy_bytes(fake.standard_error);
        out_result->standard_error_length = fake.standard_error.size();
        (void)std::snprintf(out_result->error_message, sizeof out_result->error_message, "%s",
                            fake.error_message.c_str());
        return PRK_STATUS_OK;
    }
};

std::string find_message(const LuaScriptRun& run, std::size_t index) {
    return index < run.results.size() ? run.results[index].message : std::string();
}

/** @brief Results of a JavaScript script run with a fake host. */
struct JsProcessRun : FakeScriptHostState {
    RegistryPointer registry{prk_create_registry(), &prk_destroy_registry};
    std::unique_ptr<PrkJsScript, decltype(&prk_destroy_js_script)> script{nullptr, &prk_destroy_js_script};
    PrkStatus load_status = PRK_STATUS_OK;
    std::string load_error;
    std::vector<RecordedTestResult> results;
    [[nodiscard]] std::string message(std::size_t index) const {
        return index < results.size() ? results[index].message : std::string();
    }
};

std::unique_ptr<JsProcessRun> load_and_run_js(std::string_view source, const PrkProcessRunner& runner,
                                              std::uint64_t clock_step_ms = 0) {
    auto run = std::make_unique<JsProcessRun>();
    run->process_runner = runner;
    run->clock_step_ms = clock_step_ms;
    const PrkScriptHost host = fixture_detail::make_fake_host(*run);
    run->script.reset(prk_create_js_script(nullptr, &host, nullptr));
    std::array<char, PRK_SCRIPT_ERROR_CAPACITY> error{};
    run->load_status = prk_load_js_test_script(run->script.get(), run->registry.get(), "sample.js", source.data(),
                                               source.size(), error.data(), error.size());
    run->load_error = error.data();
    if (run->load_status == PRK_STATUS_OK) {
        const PrkReporter reporter{nullptr, &fixture_detail::record_test_finished, nullptr, &run->results};
        (void)prk_run_test_cases(run->registry.get(), nullptr, &reporter, nullptr);
    }
    return run;
}

std::string find_spawn_child_path() {
#if defined(_WIN32)
    std::wstring path(32768, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    const int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(size > 0 ? size : 1), '\0');
    (void)WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, text.data(), size, nullptr, nullptr);
    text.resize(text.find('\0'));
    const char* const name = "prk_spawn_child.exe";
#else
    std::string text(4096, '\0');
    const ssize_t length = readlink("/proc/self/exe", text.data(), text.size() - 1);
    text.resize(length > 0 ? static_cast<std::size_t>(length) : 0);
    const char* const name = "prk_spawn_child";
#endif
    return text.substr(0, text.find_last_of("/\\") + 1) + name;
}

/** @brief Escapes @p text as a Lua / JavaScript single-quoted string body. */
std::string quote_script_string(const std::string& text) {
    std::string quoted = "'";
    for (const char character : text) {
        if (character == '\\' || character == '\'') {
            quoted += '\\';
        }
        quoted += character;
    }
    return quoted + "'";
}

void test_caps_script_process_timeout(PrkTestContext* context, void*) {
    PRK_EXPECT(prk_cap_script_process_timeout_ms(0, 0, 500) == PRK_PROCESS_DEFAULT_TIMEOUT_MS);
    PRK_EXPECT(prk_cap_script_process_timeout_ms(5000, 0, 500) == 5000);
    PRK_EXPECT(prk_cap_script_process_timeout_ms(5000, 1700, 1000) == 700);
    PRK_EXPECT(prk_cap_script_process_timeout_ms(300, 1700, 1000) == 300);
    PRK_EXPECT(prk_cap_script_process_timeout_ms(0, 900, 1000) == 1);
}

void test_lua_run_process_passes_request(PrkTestContext* context, void*) {
    FakeProcessRunner fake;
    fake.standard_output = "built\n";
    LuaScriptRunSetup setup;
    setup.process_runner = fake.make_runner();
    const auto run = load_and_run_lua_script(
        "prk.add_test('spawn', function()\n"
        "  local r = prk.run_process{program = 'gcc', args = {'-c', 'a b.c'}, cwd = 'build', env = {LANG = 'C'},\n"
        "    inherit_env = false, stdin = 'in', timeout_ms = 400, output_limit = 64}\n"
        "  prk.expect_true(r.ok) prk.expect_equal(r.outcome, 'exited') prk.expect_equal(r.exit_code, 0)\n"
        "  prk.expect_equal(r.stdout, 'built\\n') prk.expect_equal(r.stderr, '') prk.expect_equal(r.elapsed_ms, 12)\n"
        "  prk.expect_exit_code(r, 0)\n"
        "end, {timeout_ms = 1000})\n",
        std::move(setup));
    PRK_EXPECT(run->results.size() == 1 && run->results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(fake.program == "gcc" && fake.arguments == std::vector<std::string>({"-c", "a b.c"}));
    PRK_EXPECT(fake.working_directory == "build" && fake.environment == std::vector<std::string>({"LANG=C"}));
    PRK_EXPECT(fake.is_environment_inherited == 0 && fake.standard_input == "in");
    PRK_EXPECT(fake.timeout_ms == 400 && fake.output_limit_bytes == 64);
}

void test_lua_process_failures_are_clear(PrkTestContext* context, void*) {
    FakeProcessRunner fake;
    fake.exit_code = 1;
    fake.standard_error = "a.c:3: error: expected ';'";
    LuaScriptRunSetup setup;
    setup.process_runner = fake.make_runner();
    const auto run = load_and_run_lua_script(
        "prk.add_test('t1', function() prk.expect_exit_code(prk.run_process{program = 'cc'}, 0) end)\n"
        "prk.add_test('t2', function() prk.run_process{program = 'cc', argz = {}} end)\n"
        "prk.add_test('t3', function() prk.run_process{args = {}} end)\n"
        "prk.add_test('t4', function() prk.expect_exit_code({}, 0) end)\n",
        std::move(setup));
    PRK_EXPECT(find_message(*run, 0) ==
               "sample.lua:1: unexpected process result: expected exit code 0, got exit code 1\n"
               "stderr: a.c:3: error: expected ';'");
    PRK_EXPECT(contains_text(find_message(*run, 1), "unknown option 'argz'"));
    PRK_EXPECT(contains_text(find_message(*run, 2), "program must be a non-empty string"));
    PRK_EXPECT(contains_text(find_message(*run, 3), "result must come from prk.run_process"));
    FakeProcessRunner missing;
    missing.outcome = PRK_PROCESS_FAILED_TO_START;
    missing.error_message = "executable not found: cc";
    LuaScriptRunSetup missing_setup;
    missing_setup.process_runner = missing.make_runner();
    const auto absent = load_and_run_lua_script(
        "prk.add_test('t', function()\n"
        "  local r = prk.run_process{program = 'cc'}\n"
        "  prk.expect_false(r.ok) prk.expect_equal(r.exit_code, nil) prk.expect_equal(r.error, 'executable not found: "
        "cc')\n"
        "  prk.expect_exit_code(r, 0)\n"
        "end)\n",
        std::move(missing_setup));
    PRK_EXPECT(find_message(*absent, 0) ==
               "sample.lua:4: unexpected process result: expected exit code 0, got failed to start: "
               "executable not found: cc");
    const auto no_runner =
        load_and_run_lua_script("prk.add_test('t', function() prk.run_process{program = 'cc'} end)\n");
    PRK_EXPECT(contains_text(find_message(*no_runner, 0), "process spawn is not available in this host"));
}

void test_js_run_process_passes_request(PrkTestContext* context, void*) {
    FakeProcessRunner fake;
    fake.exit_code = 2;
    fake.standard_output = "out";
    fake.standard_error = "boom";
    const auto run = load_and_run_js(
        "prk.addTest('spawn', () => {\n"
        "  const r = prk.runProcess({program: 'node', args: ['x.js', ''], cwd: 'web', env: {A: '1', B: 'two'},\n"
        "    stdin: 'data', timeoutMs: 250, outputLimit: 99});\n"
        "  prk.expectFalse(r.ok); prk.expectEqual(r.exitCode, 2); prk.expectEqual(r.stdout, 'out');\n"
        "  prk.expectEqual(r.outcome, 'exited'); prk.expectExitCode(r, 2);\n"
        "});\n"
        "prk.addTest('fails', () => prk.expectExitCode(prk.runProcess({program: 'node'}), 0, 'node run'));\n"
        "prk.addTest('typo', () => prk.runProcess({program: 'node', timeout: 5}));\n",
        fake.make_runner());
    PRK_EXPECT(run->results.size() == 3 && run->results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(fake.program == "node" && fake.arguments == std::vector<std::string>({"x.js", ""}));
    PRK_EXPECT(fake.environment == std::vector<std::string>({"A=1", "B=two"}) && fake.is_environment_inherited == 1);
    PRK_EXPECT(fake.working_directory == "web" && fake.standard_input == "data");
    PRK_EXPECT(fake.timeout_ms == 250 && fake.output_limit_bytes == 99);
    PRK_EXPECT(run->message(1) == "sample.js:7: node run: expected exit code 0, got exit code 2\nstderr: boom");
    PRK_EXPECT(contains_text(run->message(2), "unknown option 'timeout'"));
    const auto no_runner = load_and_run_js("prk.addTest('t', () => prk.runProcess({program: 'x'}));\n",
                                           PrkProcessRunner{nullptr, nullptr});
    PRK_EXPECT(contains_text(no_runner->message(0), "process spawn is not available in this host"));
}

void test_js_process_timeout_is_capped_by_test(PrkTestContext* context, void*) {
    FakeProcessRunner fake;
    const auto run = load_and_run_js(
        "prk.addTest('t', () => { prk.runProcess({program: 'x', timeoutMs: 60000}); }, {timeoutMs: 300});\n",
        fake.make_runner());
    PRK_EXPECT(fake.spawn_count == 1 && fake.timeout_ms > 0 && fake.timeout_ms <= 300);
}

void test_scripts_spawn_real_processes(PrkTestContext* context, void*) {
    const std::string child = quote_script_string(find_spawn_child_path());
    LuaScriptRunSetup setup;
    setup.process_runner = prk_get_system_process_runner();
    const auto lua = load_and_run_lua_script(
        "prk.add_test('real', function()\n"
        "  local r = prk.run_process{program = " +
            child +
            ", args = {'echo', 'two words'}}\n"
            "  prk.expect_exit_code(r, 0) prk.expect_equal(r.stdout:gsub('\\r', ''), '[two words]\\n')\n"
            "  prk.expect_exit_code(prk.run_process{program = " +
            child +
            ", args = {'exit', '3'}}, 3)\n"
            "  prk.expect_equal(prk.run_process{program = 'prk_no_such_tool_4711'}.outcome, 'failed to start')\n"
            "end)\n",
        std::move(setup));
    PRK_EXPECT(lua->results.size() == 1 && lua->results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(find_message(*lua, 0).empty());
    const auto js = load_and_run_js(
        "prk.addTest('real', () => {\n"
        "  const r = prk.runProcess({program: " +
            child +
            ", args: ['cat'], stdin: 'piped'});\n"
            "  prk.expectExitCode(r, 0); prk.expectEqual(r.stdout, 'piped');\n"
            "});\n",
        prk_get_system_process_runner());
    PRK_EXPECT(js->results.size() == 1 && js->message(0).empty());
}

}  // namespace

PrkStatus register_script_process_api_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"script_process.caps_timeout_to_the_test", &test_caps_script_process_timeout, nullptr},
        {"script_process.lua_passes_request", &test_lua_run_process_passes_request, nullptr},
        {"script_process.lua_failures_are_clear", &test_lua_process_failures_are_clear, nullptr},
        {"script_process.js_passes_request", &test_js_run_process_passes_request, nullptr},
        {"script_process.js_timeout_is_capped_by_test", &test_js_process_timeout_is_capped_by_test, nullptr},
        {"script_process.scripts_spawn_real_processes", &test_scripts_spawn_real_processes, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
