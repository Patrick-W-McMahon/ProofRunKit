/**
 * @file test_lua_engine.cpp
 * @brief Tests for the sandboxed Lua 5.4 engine C ABI (include/proofrunkit/prk_lua.h, #9).
 */
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cpp/reporters.hpp"
#include "proofrunkit/prk_lua.h"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

using RegistryPointer = std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)>;
using LuaScriptPointer = std::unique_ptr<PrkLuaScript, decltype(&prk_destroy_lua_script)>;

/** @brief Output callback that appends printed text to a std::string. */
void append_printed_text(void* output_state, const char* text, std::size_t length) {
    static_cast<std::string*>(output_state)->append(text, length);
}

void record_test_finished(void* state, const PrkTestResult* result) {
    static_cast<std::vector<RecordedTestResult>*>(state)->push_back(record_test_result(*result));
}

/** @brief A loaded script, its registry, and what loading and running it produced. */
struct LuaScriptRun {
    RegistryPointer registry{prk_create_registry(), &prk_destroy_registry};
    std::string printed;
    LuaScriptPointer script{nullptr, &prk_destroy_lua_script};
    PrkStatus load_status = PRK_STATUS_OK;
    std::string load_error;
    std::vector<RecordedTestResult> results;
};

/** @brief Loads @p source with @p limits and, when it loads, runs every test it registered. */
std::unique_ptr<LuaScriptRun> load_and_run_lua_script(std::string_view source, const PrkLuaLimits* limits = nullptr) {
    auto run = std::make_unique<LuaScriptRun>();
    const PrkLuaOutput output{&append_printed_text, &run->printed};
    run->script.reset(prk_create_lua_script(limits, &output));
    std::array<char, PRK_LUA_ERROR_CAPACITY> error{};
    run->load_status = prk_load_lua_test_script(run->script.get(), run->registry.get(), "sample.lua", source.data(),
                                                source.size(), error.data(), error.size());
    run->load_error = error.data();
    if (run->load_status == PRK_STATUS_OK) {
        const PrkReporter reporter{nullptr, &record_test_finished, nullptr, &run->results};
        (void)prk_run_test_cases(run->registry.get(), nullptr, &reporter, nullptr);
    }
    return run;
}

bool contains_text(const std::string& text, std::string_view expected) {
    return text.find(expected) != std::string::npos;
}

void test_registers_and_runs_tests(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.add_test('lua.passes', function() prk.expect_equal(2 + 2, 4) prk.expect_true(true) end)\n"
        "prk.add_test('lua.fails', function() prk.expect_equal(2 + 2, 5, 'sum') end)\n"
        "prk.add_test('lua.skips', function() prk.skip_test('later') end)\n"
        "prk.add_test('lua.fail_test', function() prk.fail_test('explicit') end)\n");
    PRK_EXPECT(run->load_status == PRK_STATUS_OK);
    PRK_EXPECT(prk_count_lua_script_tests(run->script.get()) == 4);
    PRK_EXPECT(run->results.size() == 4);
    PRK_EXPECT(run->results[0].name == "lua.passes" && run->results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(run->results[1].outcome == PRK_OUTCOME_FAILED);
    PRK_EXPECT(run->results[1].message == "sample.lua:2: sum: expected 5, got 4");
    PRK_EXPECT(run->results[2].outcome == PRK_OUTCOME_SKIPPED && run->results[2].message == "later");
    PRK_EXPECT(run->results[3].outcome == PRK_OUTCOME_FAILED && run->results[3].message == "sample.lua:4: explicit");
}

void test_lua_errors_fail_with_traceback(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.add_test('lua.error', function() error('boom') end)\n"
        "prk.add_test('lua.assert', function() assert(false, 'nope') end)\n"
        "prk.add_test('lua.table_error', function() error({}) end)\n");
    PRK_EXPECT(run->results.size() == 3);
    PRK_EXPECT(run->results[0].outcome == PRK_OUTCOME_FAILED);
    PRK_EXPECT(run->results[0].message.find("sample.lua:1: boom") == 0);
    PRK_EXPECT(contains_text(run->results[0].message, "stack traceback"));
    PRK_EXPECT(run->results[1].message.find("sample.lua:2: nope") == 0);
    PRK_EXPECT(run->results[2].message.find("(error object is a table value)") == 0);
}

void test_reports_syntax_and_load_errors(PrkTestContext* context, void*) {
    const auto syntax = load_and_run_lua_script("prk.add_test('x', function() end\n");
    PRK_EXPECT(syntax->load_status == PRK_STATUS_SCRIPT_ERROR);
    PRK_EXPECT(syntax->load_error.find("sample.lua:2:") == 0);
    const auto runtime = load_and_run_lua_script("local x = nil\nx.field = 1\n");
    PRK_EXPECT(runtime->load_status == PRK_STATUS_SCRIPT_ERROR);
    PRK_EXPECT(runtime->load_error.find("sample.lua:2: attempt to index") == 0);
    const auto outside = load_and_run_lua_script("prk.expect_true(false)\n");
    PRK_EXPECT(contains_text(outside->load_error, "prk.expect_true can only be called inside a test body"));
    const auto duplicate =
        load_and_run_lua_script("prk.add_test('same', function() end)\nprk.add_test('same', function() end)\n");
    PRK_EXPECT(duplicate->load_status == PRK_STATUS_DUPLICATE_NAME);
    PRK_EXPECT(contains_text(duplicate->load_error, "could not register test 'same': duplicate name"));
}

void test_rejects_precompiled_bytecode(PrkTestContext* context, void*) {
    const std::string bytecode = std::string("\x1bLua") + std::string(32, '\0');
    const auto run = load_and_run_lua_script(bytecode);
    PRK_EXPECT(run->load_status == PRK_STATUS_SCRIPT_ERROR);
    PRK_EXPECT(contains_text(run->load_error, "binary chunk"));
}

void test_sandbox_hides_ambient_access(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.add_test('sandbox', function()\n"
        "  for _, name in ipairs({'io', 'os', 'package', 'debug', 'require', 'dofile', 'loadfile'}) do\n"
        "    prk.expect_equal(_G[name], nil, name)\n"
        "  end\n"
        "  prk.expect_equal(string.dump, nil, 'string.dump')\n"
        "  prk.expect_equal(load('return 40 + 2')(), 42)\n"
        "  local chunk, message = load('\\27Lua', 'bytes', 'b')\n"
        "  prk.expect_true(chunk == nil and message:find('binary chunk') ~= nil, message)\n"
        "  prk.expect_equal(type(string.format), 'function')\n"
        "  prk.expect_equal(type(table.concat), 'function')\n"
        "  prk.expect_equal(type(coroutine.wrap), 'function')\n"
        "end)\n");
    PRK_EXPECT(run->load_status == PRK_STATUS_OK);
    PRK_EXPECT(run->results.size() == 1);
    PRK_EXPECT(run->results[0].outcome == PRK_OUTCOME_PASSED);
}

void test_enforces_instruction_limit(PrkTestContext* context, void*) {
    const PrkLuaLimits limits{0, 100000};
    const auto run = load_and_run_lua_script(
        "prk.add_test('spin', function() while true do end end)\n"
        "prk.add_test('spin_in_pcall', function() while true do pcall(function() while true do end end) end end)\n"
        "prk.add_test('spin_in_coroutine', function() coroutine.wrap(function() while true do end end)() end)\n"
        "prk.add_test('budget_resets', function() for i = 1, 1000 do end end)\n",
        &limits);
    PRK_EXPECT(run->results.size() == 4);
    for (std::size_t index = 0; index < 3; ++index) {
        PRK_EXPECT(run->results[index].outcome == PRK_OUTCOME_FAILED);
        PRK_EXPECT(contains_text(run->results[index].message, "instruction limit of 100000 instructions exceeded"));
    }
    PRK_EXPECT(run->results[3].outcome == PRK_OUTCOME_PASSED);
    const auto loading = load_and_run_lua_script("while true do end\n", &limits);
    PRK_EXPECT(loading->load_status == PRK_STATUS_SCRIPT_ERROR);
    PRK_EXPECT(contains_text(loading->load_error, "instruction limit"));
}

void test_enforces_memory_limit(PrkTestContext* context, void*) {
    const PrkLuaLimits limits{1024 * 1024, 0};
    const auto run = load_and_run_lua_script(
        "prk.add_test('grow', function() local parts = {} for i = 1, 1e7 do parts[i] = ('x'):rep(64) .. i end end)\n"
        "prk.add_test('huge_string', function() local s = ('x'):rep(1 << 30) end)\n"
        "prk.add_test('recovers', function() prk.expect_equal(#('x'):rep(100), 100) end)\n",
        &limits);
    PRK_EXPECT(run->results.size() == 3);
    PRK_EXPECT(run->results[0].outcome == PRK_OUTCOME_FAILED);
    PRK_EXPECT(run->results[0].message == "memory limit of 1048576 bytes exceeded");
    PRK_EXPECT(run->results[1].message == "memory limit of 1048576 bytes exceeded");
    PRK_EXPECT(run->results[2].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(prk_get_lua_script_memory_in_use(run->script.get()) <= limits.memory_limit_bytes);
}

void test_print_goes_to_output_callback(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "print('loading', 1, nil, true)\n"
        "prk.add_test('prints', function() print('in test') end)\n");
    PRK_EXPECT(run->printed == "loading\t1\tnil\ttrue\nin test\n");
}

void test_add_test_only_while_loading(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.add_test('nested', function() prk.add_test('late', function() end) end)\n"
        "prk.add_test('bad_args', function() end)\n");
    PRK_EXPECT(run->results.size() == 2);
    PRK_EXPECT(contains_text(run->results[0].message, "prk.add_test can only be called while the script loads"));
    const auto bad = load_and_run_lua_script("prk.add_test(42)\n");
    PRK_EXPECT(bad->load_status == PRK_STATUS_SCRIPT_ERROR);
    PRK_EXPECT(contains_text(bad->load_error, "bad argument #2 to 'add_test'"));
}

void test_rejects_invalid_arguments(PrkTestContext* context, void*) {
    const RegistryPointer registry(prk_create_registry(), &prk_destroy_registry);
    const LuaScriptPointer script(prk_create_lua_script(nullptr, nullptr), &prk_destroy_lua_script);
    PRK_EXPECT(script != nullptr);
    PRK_EXPECT(prk_load_lua_test_script(nullptr, registry.get(), "a", "", 0, nullptr, 0) ==
               PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_load_lua_test_script(script.get(), nullptr, "a", "", 0, nullptr, 0) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_load_lua_test_script(script.get(), registry.get(), "a", "", 0, nullptr, 0) == PRK_STATUS_OK);
    PRK_EXPECT(prk_load_lua_test_script(script.get(), registry.get(), "a", "", 0, nullptr, 0) ==
               PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_count_lua_script_tests(nullptr) == 0 && prk_get_lua_script_memory_in_use(nullptr) == 0);
    prk_destroy_lua_script(nullptr);
}

void test_reports_versions(PrkTestContext* context, void*) {
    PRK_EXPECT(std::strcmp(prk_get_lua_version_string(), "Lua 5.4.9") == 0);
    const auto run = load_and_run_lua_script(
        "prk.add_test('versions', function()\n"
        "  prk.expect_equal(prk.lua_version, 'Lua 5.4.9')\n"
        "  prk.expect_equal(type(prk.version), 'string')\n"
        "end)\n");
    PRK_EXPECT(run->results.size() == 1 && run->results[0].outcome == PRK_OUTCOME_PASSED);
}

}  // namespace

PrkStatus register_lua_engine_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"lua.engine.registers_and_runs_tests", &test_registers_and_runs_tests, nullptr},
        {"lua.engine.lua_errors_fail_with_traceback", &test_lua_errors_fail_with_traceback, nullptr},
        {"lua.engine.reports_syntax_and_load_errors", &test_reports_syntax_and_load_errors, nullptr},
        {"lua.sandbox.rejects_precompiled_bytecode", &test_rejects_precompiled_bytecode, nullptr},
        {"lua.sandbox.hides_ambient_access", &test_sandbox_hides_ambient_access, nullptr},
        {"lua.sandbox.enforces_instruction_limit", &test_enforces_instruction_limit, nullptr},
        {"lua.sandbox.enforces_memory_limit", &test_enforces_memory_limit, nullptr},
        {"lua.sandbox.print_goes_to_output_callback", &test_print_goes_to_output_callback, nullptr},
        {"lua.api.add_test_only_while_loading", &test_add_test_only_while_loading, nullptr},
        {"lua.engine.rejects_invalid_arguments", &test_rejects_invalid_arguments, nullptr},
        {"lua.engine.reports_versions", &test_reports_versions, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
