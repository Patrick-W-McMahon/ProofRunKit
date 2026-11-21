/**
 * @file test_lua_test_api.cpp
 * @brief Internal tests for the Lua test-authoring API (#12): file reads against a custom memory limit.
 *        Cases that prk scripts cover are in prk.tests.json (docs/self-hosting.md, #96).
 */
#include <string>

#include "lua_script_fixture.hpp"

namespace prk::selftest {

namespace {

/** @brief Returns the message of result @p index, or "" when there is no such result. */
std::string find_message(const LuaScriptRun& run, std::size_t index) {
    return index < run.results.size() ? run.results[index].message : std::string();
}

void test_file_reads_respect_memory_limit(PrkTestContext* context, void*) {
    const PrkLuaLimits limits{512 * 1024, 0};
    LuaScriptRunSetup setup;
    setup.limits = &limits;
    setup.files = {{"big.bin", std::string(1024 * 1024, 'x')}};
    const auto run =
        load_and_run_lua_script("prk.add_test('big', function() prk.read_text_file('big.bin') end)\n", setup);
    PRK_EXPECT(contains_text(find_message(*run, 0), "does not fit in the script memory limit"));
}

}  // namespace

PrkStatus register_lua_test_api_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"lua.api.file_reads_respect_memory_limit", &test_file_reads_respect_memory_limit, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
