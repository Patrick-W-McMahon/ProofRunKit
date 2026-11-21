/**
 * @file test_lua_engine.cpp
 * @brief Tests for the sandboxed Lua 5.4 engine C ABI (include/proofrunkit/prk_lua.h, #9).
 *        The test-authoring API (#12) is covered in test_lua_test_api.cpp.
 */

#include "lua_script_fixture.hpp"

namespace prk::selftest {

namespace {

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

void test_rejects_invalid_arguments(PrkTestContext* context, void*) {
    const RegistryPointer registry(prk_create_registry(), &prk_destroy_registry);
    const LuaScriptPointer script(prk_create_lua_script(nullptr, nullptr, nullptr), &prk_destroy_lua_script);
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

}  // namespace

PrkStatus register_lua_engine_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"lua.engine.lua_errors_fail_with_traceback", &test_lua_errors_fail_with_traceback, nullptr},
        {"lua.sandbox.enforces_instruction_limit", &test_enforces_instruction_limit, nullptr},
        {"lua.engine.rejects_invalid_arguments", &test_rejects_invalid_arguments, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
