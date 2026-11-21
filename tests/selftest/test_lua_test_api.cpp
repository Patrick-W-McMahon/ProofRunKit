/**
 * @file test_lua_test_api.cpp
 * @brief Tests for the Lua test-authoring API (#12): assertions, error objects, suites and hooks,
 *        tags and name filters, per-test timeouts, timing, and read-only file checks.
 */
#include <string>
#include <vector>

#include "lua_script_fixture.hpp"

namespace prk::selftest {

namespace {

/** @brief Returns the message of result @p index, or "" when there is no such result. */
std::string find_message(const LuaScriptRun& run, std::size_t index) {
    return index < run.results.size() ? run.results[index].message : std::string();
}

void test_assertions_pass_on_good_values(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.add_test('all', function()\n"
        "  prk.expect_true(1) prk.expect_false(nil) prk.expect_false(false)\n"
        "  prk.expect_equal('a', 'a') prk.expect_not_equal(1, 2) prk.expect_nil(nil) prk.expect_not_nil(0)\n"
        "  prk.expect_near(0.1 + 0.2, 0.3, 1e-9) prk.expect_less_than(1, 2) prk.expect_less_or_equal(2, 2)\n"
        "  prk.expect_greater_than(3, 2) prk.expect_greater_or_equal(3, 3)\n"
        "  prk.expect_contains('hello world', 'o w') prk.expect_contains({1, 'x', 3}, 'x')\n"
        "  prk.expect_match('v1.2.3', '^v%d+%.%d+%.%d+$') prk.expect_type({}, 'table')\n"
        "  prk.expect_deep_equal({a = {1, 2, {b = true}}}, {a = {1, 2, {b = true}}})\n"
        "  local cycle = {} cycle.self = cycle local other = {} other.self = other\n"
        "  prk.expect_deep_equal(cycle, other)\n"
        "  prk.expect_equal(prk.expect_error(function() error('bad input') end, 'bad'), 'sample.lua:11: bad input')\n"
        "end)\n");
    PRK_EXPECT(run->load_status == PRK_STATUS_OK);
    PRK_EXPECT(run->results.size() == 1 && run->results[0].outcome == PRK_OUTCOME_PASSED);
}

void test_assertions_report_clear_failures(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.add_test('t1', function() prk.expect_false('yes') end)\n"
        "prk.add_test('t2', function() prk.expect_not_equal(4, 4, 'ids') end)\n"
        "prk.add_test('t3', function() prk.expect_near(1.0, 2.0, 0.5) end)\n"
        "prk.add_test('t4', function() prk.expect_less_than(5, 3) end)\n"
        "prk.add_test('t5', function() prk.expect_contains({1, 2}, 3) end)\n"
        "prk.add_test('t6', function() prk.expect_match('abc', '^%d') end)\n"
        "prk.add_test('t7', function() prk.expect_type(1, 'string') end)\n"
        "prk.add_test('t8', function() prk.expect_deep_equal({a = {1, 2}}, {a = {1, 3}}) end)\n"
        "prk.add_test('t9', function() prk.expect_error(function() end) end)\n"
        "prk.add_test('t10', function() prk.expect_error(function() error('x') end, 'y') end)\n"
        "prk.add_test('t11', function() prk.expect_nil(7) end)\n"
        "prk.add_test('t12', function() prk.expect_deep_equal({}, {k = 1}) end)\n"
        "prk.add_test('t13', function() prk.expect_deep_equal({k = 1}, {}) end)\n");
    const std::vector<std::string> expected = {
        "sample.lua:1: expected a false value, got \"yes\"",
        "sample.lua:2: ids: both are 4",
        "sample.lua:3: values are not near: expected 2.0 +/- 0.5, got 1.0",
        "sample.lua:4: value too large: expected 5 < 3",
        "sample.lua:5: item not found: expected ",  // the table address varies
        "sample.lua:6: no match: expected \"abc\" to match \"^%d\"",
        "sample.lua:7: wrong type: expected string, got number",
        "sample.lua:8: values differ: at value[\"a\"][2]: expected 3, got 2",
        "sample.lua:9: expected an error, but the function returned normally",
        "sample.lua:10: wrong error: expected an error matching \"y\", got \"sample.lua:10: x\"",
        "sample.lua:11: expected nil: got 7",
        "sample.lua:12: values differ: at value[\"k\"]: expected 1, got nil",
        "sample.lua:13: values differ: at value[\"k\"]: unexpected 1",
    };
    PRK_EXPECT(run->results.size() == expected.size());
    for (std::size_t index = 0; index < expected.size() && index < run->results.size(); ++index) {
        PRK_EXPECT(run->results[index].outcome == PRK_OUTCOME_FAILED);
        PRK_EXPECT(find_message(*run, index).starts_with(expected[index]));
    }
}

void test_failures_are_catchable_error_objects(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.add_test('caught', function()\n"
        "  local ok, failure = pcall(prk.expect_equal, 1, 2)\n"
        "  prk.expect_false(ok)\n"
        "  prk.expect_match(tostring(failure), 'expected 2, got 1')\n"
        "  prk.expect_match(prk.expect_error(function() prk.fail_test('inner') end), 'inner$')\n"
        "  prk.expect_match(prk.expect_error(function() prk.skip_test('why') end), '^why$')\n"
        "end)\n"
        "prk.add_test('rethrown', function()\n"
        "  local ok, failure = pcall(prk.fail_test, 'kept')\n"
        "  error(failure)\n"
        "end)\n");
    PRK_EXPECT(run->results.size() == 2);
    PRK_EXPECT(run->results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(run->results[1].outcome == PRK_OUTCOME_FAILED && find_message(*run, 1) == "kept");
}

void test_suites_name_tests_and_order_hooks(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.before_all(function() print('root before_all') end)\n"
        "prk.after_all(function() print('root after_all') end)\n"
        "prk.describe('outer', function()\n"
        "  prk.before_all(function() print('outer before_all') end)\n"
        "  prk.before_each(function() print('outer before_each') end)\n"
        "  prk.after_each(function() print('outer after_each') end)\n"
        "  prk.after_all(function() print('outer after_all') end)\n"
        "  prk.describe('inner', function()\n"
        "    prk.before_each(function() print('inner before_each') end)\n"
        "    prk.after_each(function() print('inner after_each') end)\n"
        "    prk.add_test('one', function() print('one') end)\n"
        "  end)\n"
        "  prk.add_test('two', function() print('two') end)\n"
        "end)\n"
        "prk.add_test('three', function() print('three') end)\n");
    PRK_EXPECT(run->load_status == PRK_STATUS_OK);
    PRK_EXPECT(run->list_test_names() == std::vector<std::string>({"outer.inner.one", "outer.two", "three"}));
    PRK_EXPECT(run->printed ==
               "root before_all\nouter before_all\nouter before_each\ninner before_each\none\n"
               "inner after_each\nouter after_each\n"
               "outer before_each\ntwo\nouter after_each\nouter after_all\n"
               "three\nroot after_all\n");
    for (const RecordedTestResult& result : run->results) {
        PRK_EXPECT(result.outcome == PRK_OUTCOME_PASSED);
    }
}

void test_hook_failures_fail_the_right_tests(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.describe('setup', function()\n"
        "  prk.before_all(function() error('no database', 0) end)\n"
        "  prk.add_test('a', function() end)\n"
        "  prk.add_test('b', function() end)\n"
        "end)\n"
        "prk.describe('each', function()\n"
        "  prk.before_each(function() prk.fail_test('dirty') end)\n"
        "  prk.after_each(function() print('cleaned') end)\n"
        "  prk.add_test('c', function() print('never') end)\n"
        "end)\n"
        "prk.describe('teardown', function()\n"
        "  prk.after_each(function() prk.expect_true(false, 'leak') end)\n"
        "  prk.add_test('d', function() end)\n"
        "end)\n"
        "prk.describe('skipping', function()\n"
        "  prk.before_each(function() prk.skip_test('no gpu') end)\n"
        "  prk.add_test('e', function() end)\n"
        "end)\n");
    PRK_EXPECT(run->results.size() == 5);
    PRK_EXPECT(find_message(*run, 0).starts_with("before_all failed: no database\nstack traceback:"));
    PRK_EXPECT(find_message(*run, 1) == find_message(*run, 0));
    PRK_EXPECT(find_message(*run, 2) == "before_each failed: sample.lua:7: dirty");
    PRK_EXPECT(run->printed == "cleaned\n");
    PRK_EXPECT(find_message(*run, 3) == "after_each failed: sample.lua:12: leak");
    PRK_EXPECT(run->results.size() == 5 && run->results[4].outcome == PRK_OUTCOME_SKIPPED);
    PRK_EXPECT(find_message(*run, 4) == "no gpu");
}

void test_tags_and_name_filter_select_tests(PrkTestContext* context, void*) {
    const char* const script =
        "prk.add_test('fast_one', function() end, {tags = {'fast'}})\n"
        "prk.describe('slow_suite', function()\n"
        "  prk.add_test('inherits', function() end)\n"
        "  prk.add_test('flaky', function() end, {tags = {'flaky'}})\n"
        "end, {tags = {'slow'}})\n"
        "prk.add_test('untagged', function() end)\n";
    const char* const include_slow[] = {"slow"};
    const char* const exclude_flaky[] = {"flaky"};
    const PrkScriptRunSettings slow_not_flaky{nullptr, include_slow, 1, exclude_flaky, 1, 0};
    LuaScriptRunSetup setup;
    setup.settings = &slow_not_flaky;
    const auto tagged = load_and_run_lua_script(script, setup);
    PRK_EXPECT(tagged->list_test_names() == std::vector<std::string>({"slow_suite.inherits"}));
    PRK_EXPECT(prk_count_lua_script_tests(tagged->script.get()) == 1);
    const PrkScriptRunSettings by_name{"suite.", nullptr, 0, nullptr, 0, 0};
    setup.settings = &by_name;
    const auto named = load_and_run_lua_script(script, setup);
    PRK_EXPECT(named->list_test_names() == std::vector<std::string>({"slow_suite.inherits", "slow_suite.flaky"}));
}

void test_skip_option_and_registration_errors(PrkTestContext* context, void*) {
    const auto run = load_and_run_lua_script(
        "prk.add_test('later', function() error('must not run') end, {skip = 'needs #13'})\n"
        "prk.add_test('flag', function() error('must not run') end, {skip = true})\n");
    PRK_EXPECT(run->results.size() == 2 && run->results[0].outcome == PRK_OUTCOME_SKIPPED);
    PRK_EXPECT(find_message(*run, 0) == "needs #13" && find_message(*run, 1) == "skipped");
    const auto typo = load_and_run_lua_script("prk.add_test('x', function() end, {timeout = 5})\n");
    PRK_EXPECT(contains_text(typo->load_error, "prk.add_test: unknown option 'timeout'"));
    const auto bad_timeout = load_and_run_lua_script("prk.add_test('x', function() end, {timeout_ms = -1})\n");
    PRK_EXPECT(contains_text(bad_timeout->load_error, "timeout_ms must be a positive integer"));
    const auto bad_suite = load_and_run_lua_script("prk.describe('a.b', function() end)\n");
    PRK_EXPECT(contains_text(bad_suite->load_error, "suite name must be non-empty without '.'"));
    const auto late_hook = load_and_run_lua_script("prk.add_test('x', function() prk.before_each(print) end)\n");
    PRK_EXPECT(contains_text(find_message(*late_hook, 0), "prk.before_each can only be called while the script loads"));
}

void test_timeouts_stop_slow_tests(PrkTestContext* context, void*) {
    LuaScriptRunSetup setup;
    setup.clock_step_ms = 1;  // The fake clock advances 1 ms per check (one check per 1000 instructions).
    const auto run = load_and_run_lua_script(
        "prk.add_test('spin', function() while true do end end, {timeout_ms = 5})\n"
        "prk.add_test('swallow', function() while true do pcall(function() while true do end end) end end,\n"
        "  {timeout_ms = 5})\n"
        "prk.add_test('quick', function() for i = 1, 100 do end end, {timeout_ms = 5})\n",
        setup);
    PRK_EXPECT(run->results.size() == 3);
    PRK_EXPECT(contains_text(find_message(*run, 0), "timeout of 5 ms exceeded"));
    PRK_EXPECT(contains_text(find_message(*run, 1), "timeout of 5 ms exceeded"));
    PRK_EXPECT(run->results.size() == 3 && run->results[2].outcome == PRK_OUTCOME_PASSED);
    const PrkScriptRunSettings default_timeout{nullptr, nullptr, 0, nullptr, 0, 7};
    setup.settings = &default_timeout;
    const auto defaulted = load_and_run_lua_script("prk.add_test('spin', function() while true do end end)\n", setup);
    PRK_EXPECT(contains_text(find_message(*defaulted, 0), "timeout of 7 ms exceeded"));
}

void test_clock_and_timing(PrkTestContext* context, void*) {
    LuaScriptRunSetup setup;
    setup.clock_step_ms = 10;
    const auto run = load_and_run_lua_script(
        "prk.add_test('clock', function()\n"
        "  local first = prk.read_clock_ms()\n"
        "  prk.expect_equal(prk.read_clock_ms() - first, 10)\n"
        "  prk.expect_equal(prk.measure_elapsed_ms(function(a, b) return a + b end, 1, 2), 10)\n"
        "end)\n",
        setup);
    PRK_EXPECT(run->results.size() == 1 && run->results[0].outcome == PRK_OUTCOME_PASSED);
    LuaScriptRunSetup no_host;
    no_host.grants_host = false;
    const auto bare = load_and_run_lua_script(
        "prk.add_test('clock', function() prk.read_clock_ms() end)\n"
        "prk.add_test('file', function() prk.is_file_present('a') end)\n",
        no_host);
    PRK_EXPECT(contains_text(find_message(*bare, 0), "no clock is available"));
    PRK_EXPECT(contains_text(find_message(*bare, 1), "file access is not available"));
}

void test_file_checks_use_the_host(PrkTestContext* context, void*) {
    LuaScriptRunSetup setup;
    setup.files = {{"out/report.txt", "status: ok\nwarnings: 0\n"}};
    const auto run = load_and_run_lua_script(
        "prk.add_test('present', function()\n"
        "  prk.expect_true(prk.is_file_present('out/report.txt'))\n"
        "  prk.expect_false(prk.is_file_present('out/missing.txt'))\n"
        "  prk.expect_file_present('out/report.txt')\n"
        "  prk.expect_file_contains('out/report.txt', 'warnings: 0')\n"
        "  prk.expect_equal(#prk.read_text_file('out/report.txt'), 23)\n"
        "end)\n"
        "prk.add_test('missing', function() prk.expect_file_present('out/missing.txt') end)\n"
        "prk.add_test('denied', function() prk.read_text_file('secret/key') end)\n"
        "prk.add_test('not_found', function() prk.read_text_file('nope') end)\n"
        "prk.add_test('wrong_text', function() prk.expect_file_contains('out/report.txt', 'error') end)\n",
        setup);
    PRK_EXPECT(run->results.size() == 5 && run->results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(find_message(*run, 1) == "sample.lua:8: expected file \"out/missing.txt\" to exist");
    PRK_EXPECT(contains_text(find_message(*run, 2), "access denied for 'secret/key'"));
    PRK_EXPECT(contains_text(find_message(*run, 3), "'nope': not found"));
    PRK_EXPECT(contains_text(find_message(*run, 4), "text not found"));
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
        {"lua.api.assertions_pass_on_good_values", &test_assertions_pass_on_good_values, nullptr},
        {"lua.api.assertions_report_clear_failures", &test_assertions_report_clear_failures, nullptr},
        {"lua.api.failures_are_catchable_error_objects", &test_failures_are_catchable_error_objects, nullptr},
        {"lua.api.suites_name_tests_and_order_hooks", &test_suites_name_tests_and_order_hooks, nullptr},
        {"lua.api.hook_failures_fail_the_right_tests", &test_hook_failures_fail_the_right_tests, nullptr},
        {"lua.api.tags_and_name_filter_select_tests", &test_tags_and_name_filter_select_tests, nullptr},
        {"lua.api.skip_option_and_registration_errors", &test_skip_option_and_registration_errors, nullptr},
        {"lua.api.timeouts_stop_slow_tests", &test_timeouts_stop_slow_tests, nullptr},
        {"lua.api.clock_and_timing", &test_clock_and_timing, nullptr},
        {"lua.api.file_checks_use_the_host", &test_file_checks_use_the_host, nullptr},
        {"lua.api.file_reads_respect_memory_limit", &test_file_reads_respect_memory_limit, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
