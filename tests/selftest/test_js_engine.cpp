/**
 * @file test_js_engine.cpp
 * @brief Tests for the embedded QuickJS-ng engine and its `prk` API (#11): the sandbox (no std/os
 *        modules, no imports, no ambient file or process access), instruction/time/memory budgets
 *        that try/catch cannot swallow, async tests, assertions, suites and hooks, tags, and files.
 */
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lua_script_fixture.hpp"
#include "proofrunkit/prk_js.h"

/** @brief Like PRK_EXPECT, for "result @p index of @p run passed"; shows the script's message if not. */
#define PRK_EXPECT_JS_PASSED(run, index)                                                                        \
    do {                                                                                                        \
        if (!(run).has_outcome((index), PRK_OUTCOME_PASSED)) {                                                  \
            prk_fail_test(context,                                                                              \
                          (std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": expected script test " + \
                           std::to_string(index) + " to pass: " + (run).message(index))                         \
                              .c_str());                                                                        \
            return;                                                                                             \
        }                                                                                                       \
    } while (false)

namespace prk::selftest {

namespace {

using JsScriptPointer = std::unique_ptr<PrkJsScript, decltype(&prk_destroy_js_script)>;

/** @brief A loaded JavaScript test script, its fake host, and what loading and running it produced. */
struct JsScriptRun : FakeScriptHostState {
    RegistryPointer registry{prk_create_registry(), &prk_destroy_registry};
    JsScriptPointer script{nullptr, &prk_destroy_js_script};
    PrkStatus load_status = PRK_STATUS_OK;
    std::string load_error;
    std::vector<RecordedTestResult> results;

    [[nodiscard]] std::string message(std::size_t index) const {
        return index < results.size() ? results[index].message : std::string();
    }
    [[nodiscard]] bool has_outcome(std::size_t index, PrkOutcome outcome) const {
        return index < results.size() && results[index].outcome == outcome;
    }
};

/** @brief Loads @p source as "sample.js" and, when it loads, runs every test it registered. */
std::unique_ptr<JsScriptRun> load_and_run_js_script(std::string_view source,
                                                    LuaScriptRunSetup setup = LuaScriptRunSetup{}) {
    auto run = std::make_unique<JsScriptRun>();
    run->clock_step_ms = setup.clock_step_ms;
    run->files = std::move(setup.files);
    run->process_runner = setup.process_runner;
    const PrkScriptHost host = fixture_detail::make_fake_host(*run);
    run->script.reset(prk_create_js_script(setup.limits, setup.grants_host ? &host : nullptr, setup.settings));
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

void test_js_reports_engine_version(PrkTestContext* context, void*) {
    PRK_EXPECT(std::string(prk_get_js_engine_version_string()) == "QuickJS-ng 0.18.0");
    const auto run = load_and_run_js_script("print(prk.jsEngine, typeof prk.version); console.log('x', 1);\n");
    PRK_EXPECT(run->load_status == PRK_STATUS_OK && run->printed == "QuickJS-ng 0.18.0 string\nx 1\n");
}

void test_js_sandbox_has_no_ambient_access(PrkTestContext* context, void*) {
    const auto run = load_and_run_js_script(
        "prk.addTest('globals', () => {\n"
        "  for (const name of ['require', 'std', 'os', 'process', 'scriptArgs', 'setTimeout', 'fetch'])\n"
        "    prk.expectEqual(typeof globalThis[name], 'undefined', name);\n"
        "});\n"
        "prk.addTest('import', async () => {\n"
        "  await prk.expectTrue(await import('qjs:os').then(() => false, () => true), 'import must fail');\n"
        "});\n"
        "prk.addTest('static import', () => {\n"
        "  try { eval(\"import 'qjs:std'\"); prk.failTest('imported'); }\n"
        "  catch (e) { prk.expectTrue(e instanceof SyntaxError, String(e)); }\n"
        "});\n");
    PRK_EXPECT(run->results.size() == 3);
    for (std::size_t index = 0; index < run->results.size(); ++index) {
        PRK_EXPECT_JS_PASSED(*run, index);
    }
}

void test_js_budget_cannot_be_caught(PrkTestContext* context, void*) {
    const PrkScriptLimits limits{0, 2000000};
    LuaScriptRunSetup setup;
    setup.limits = &limits;
    const auto run = load_and_run_js_script(
        "prk.addTest('spin', () => { for (;;) { try { for (;;) {} } catch (e) {} } });\n"
        "prk.addTest('after', () => {});\n",
        setup);
    PRK_EXPECT(run->has_outcome(0, PRK_OUTCOME_FAILED));
    PRK_EXPECT(run->message(0) == "instruction limit of 2000000 instructions exceeded");
    PRK_EXPECT(run->has_outcome(1, PRK_OUTCOME_PASSED));  // Each test gets a fresh budget.
    const auto at_load = load_and_run_js_script("while (true) {}\n", setup);
    PRK_EXPECT(at_load->load_status != PRK_STATUS_OK);
    PRK_EXPECT(contains_text(at_load->load_error, "instruction limit of 2000000 instructions exceeded"));
}

void test_js_timeout_and_memory_limits(PrkTestContext* context, void*) {
    LuaScriptRunSetup timed;
    timed.clock_step_ms = 5;
    const auto slow = load_and_run_js_script(
        "prk.addTest('slow', () => { try { for (;;) {} } finally { for (;;) {} } }, {timeoutMs: 50});\n", timed);
    PRK_EXPECT(slow->has_outcome(0, PRK_OUTCOME_FAILED) && slow->message(0) == "timeout of 50 ms exceeded");
    const PrkScriptLimits small{4u * 1024u * 1024u, 0};
    LuaScriptRunSetup limited;
    limited.limits = &small;
    const auto hungry = load_and_run_js_script(
        "prk.addTest('hungry', () => { const keep = []; for (;;) keep.push(new Array(4096).fill(7)); });\n"
        "prk.addTest('after', () => prk.expectEqual([1, 2].length, 2));\n",
        limited);
    PRK_EXPECT(hungry->has_outcome(0, PRK_OUTCOME_FAILED));
    PRK_EXPECT(hungry->message(0) == "memory limit of 4194304 bytes exceeded");
    PRK_EXPECT_JS_PASSED(*hungry, 1);
    const auto deep = load_and_run_js_script("prk.addTest('deep', () => { const f = () => f() + 1; f(); });\n");
    PRK_EXPECT(deep->has_outcome(0, PRK_OUTCOME_FAILED));
    PRK_EXPECT(deep->message(0).starts_with("RangeError: Maximum call stack size exceeded"));
}

void test_js_assertions_and_locations(PrkTestContext* context, void*) {
    const auto run = load_and_run_js_script(
        "prk.addTest('good', () => {\n"
        "  prk.expectTrue(1); prk.expectFalse(''); prk.expectEqual('a', 'a'); prk.expectNotEqual(1, '1');\n"
        "  prk.expectNullish(undefined); prk.expectNotNullish(0); prk.expectNear(0.1 + 0.2, 0.3, 1e-9);\n"
        "  prk.expectLessThan(1, 2); prk.expectGreaterOrEqual(2, 2); prk.expectContains([1, 'x'], 'x');\n"
        "  prk.expectMatch('v1.2', /^v\\d+\\.\\d+$/); prk.expectType({}, 'object');\n"
        "  prk.expectDeepEqual({a: [1, {b: true}]}, {a: [1, {b: true}]});\n"
        "  prk.expectEqual(prk.expectError(() => { throw new TypeError('bad input'); }, /bad/), 'bad input');\n"
        "});\n"
        "prk.addTest('t1', () => prk.expectEqual(1, 2));\n"
        "prk.addTest('t2', () => prk.expectDeepEqual({a: [1, 2]}, {a: [1, 3]}, 'shape'));\n"
        "prk.addTest('t3', () => prk.expectError(() => {}));\n"
        "prk.addTest('t4', () => { throw new Error('plain'); });\n"
        "prk.addTest('t5', () => prk.failTest('custom'));\n"
        "prk.addTest('t6', () => prk.skipTest('not today'));\n"
        "prk.addTest('t7', () => { try { prk.expectTrue(false); } catch (e) {\n"
        "  prk.expectTrue(e instanceof prk.AssertionError); } });\n"
        "prk.addTest('t8', () => {}, {skip: true});\n");
    PRK_EXPECT_JS_PASSED(*run, 0);
    PRK_EXPECT(run->message(1) == "sample.js:9: values differ: expected 2, got 1");
    PRK_EXPECT(run->message(2) == "sample.js:10: shape: at value[\"a\"][1]: expected 3, got 2");
    PRK_EXPECT(run->message(3) == "sample.js:11: expected an error, but the function returned normally");
    PRK_EXPECT(run->message(4).starts_with("Error: plain\n    at "));
    PRK_EXPECT(run->message(5) == "sample.js:13: custom");
    PRK_EXPECT(run->has_outcome(6, PRK_OUTCOME_SKIPPED) && run->message(6) == "not today");
    PRK_EXPECT(run->has_outcome(7, PRK_OUTCOME_PASSED));
    PRK_EXPECT(run->has_outcome(8, PRK_OUTCOME_SKIPPED) && run->message(8) == "skipped");
}

void test_js_async_tests(PrkTestContext* context, void*) {
    const auto run = load_and_run_js_script(
        "prk.addTest('ok', async () => { const v = await Promise.resolve(3); prk.expectEqual(v, 3); });\n"
        "prk.addTest('rejects', async () => { await null; prk.failTest('later'); });\n"
        "prk.addTest('never', () => new Promise(() => {}));\n"
        "prk.addTest('skips', async () => { await null; prk.skipTest('async skip'); });\n");
    PRK_EXPECT(run->has_outcome(0, PRK_OUTCOME_PASSED));
    PRK_EXPECT(run->message(1) == "sample.js:2: later");
    PRK_EXPECT(run->has_outcome(2, PRK_OUTCOME_FAILED) && contains_text(run->message(2), "never settled"));
    PRK_EXPECT(run->has_outcome(3, PRK_OUTCOME_SKIPPED) && run->message(3) == "async skip");
}

void test_js_suites_hooks_and_tags(PrkTestContext* context, void*) {
    const char* const excluded[] = {"slow"};
    PrkScriptRunSettings settings{nullptr, nullptr, 0, excluded, 1, 0};
    LuaScriptRunSetup setup;
    setup.settings = &settings;
    const auto run = load_and_run_js_script(
        "const log = [];\n"
        "prk.describe('db', () => {\n"
        "  prk.beforeAll(() => log.push('ba')); prk.afterAll(() => print(log.join(',')));\n"
        "  prk.beforeEach(() => log.push('be')); prk.afterEach(() => log.push('ae'));\n"
        "  prk.addTest('one', () => log.push('t1'));\n"
        "  prk.describe('inner', () => { prk.addTest('two', () => log.push('t2')); });\n"
        "  prk.addTest('slow', () => log.push('never'), {tags: ['slow']});\n"
        "});\n"
        "prk.describe('broken', () => {\n"
        "  prk.beforeAll(() => { throw new Error('no database'); });\n"
        "  prk.addTest('a', () => {});\n"
        "});\n",
        setup);
    PRK_EXPECT(prk_count_test_cases(run->registry.get()) == 3);
    PRK_EXPECT(run->results.size() == 3 && run->results[1].name == "db.inner.two");
    PRK_EXPECT(run->has_outcome(0, PRK_OUTCOME_PASSED) && run->has_outcome(1, PRK_OUTCOME_PASSED));
    PRK_EXPECT(run->printed == "ba,be,t1,ae,be,t2,ae\n");
    PRK_EXPECT(run->message(2).starts_with("before_all failed: Error: no database"));
}

void test_js_rejects_bad_registration(PrkTestContext* context, void*) {
    const auto unknown = load_and_run_js_script("prk.addTest('t', () => {}, {timeout: 5});\n");
    PRK_EXPECT(unknown->load_status != PRK_STATUS_OK && contains_text(unknown->load_error, "unknown option 'timeout'"));
    const auto nested = load_and_run_js_script("prk.addTest('t', () => prk.addTest('u', () => {}));\n");
    PRK_EXPECT(contains_text(nested->message(0), "only be called while the script loads"));
    const auto syntax = load_and_run_js_script("prk.addTest('t', () => {\n");
    PRK_EXPECT(syntax->load_status != PRK_STATUS_OK && syntax->load_error.starts_with("sample.js:2: SyntaxError: "));
    const auto open_suite = load_and_run_js_script("prk.describe('s', () => { throw new Error('mid'); });\n");
    PRK_EXPECT(open_suite->load_status != PRK_STATUS_OK && contains_text(open_suite->load_error, "mid"));
}

void test_js_reads_only_granted_files(PrkTestContext* context, void*) {
    LuaScriptRunSetup setup;
    setup.files = {{"data/config.txt", "mode=fast\n"}};
    setup.clock_step_ms = 7;
    const auto run = load_and_run_js_script(
        "prk.addTest('files', () => {\n"
        "  prk.expectFilePresent('data/config.txt'); prk.expectFalse(prk.isFilePresent('data/none'));\n"
        "  prk.expectFileContains('data/config.txt', 'mode=fast');\n"
        "  prk.expectError(() => prk.readTextFile('secret/key'), 'access denied');\n"
        "  prk.expectError(() => prk.readTextFile('data/none'), 'not found');\n"
        "  prk.expectEqual(prk.measureElapsedMs(() => {}), 7);\n"
        "});\n",
        setup);
    PRK_EXPECT_JS_PASSED(*run, 0);
    LuaScriptRunSetup no_host;
    no_host.grants_host = false;
    const auto denied = load_and_run_js_script("prk.addTest('t', () => prk.readTextFile('a.txt'));\n", no_host);
    PRK_EXPECT(contains_text(denied->message(0), "file access is not available"));
}

}  // namespace

PrkStatus register_js_engine_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"js.reports_engine_version", &test_js_reports_engine_version, nullptr},
        {"js.sandbox_has_no_ambient_access", &test_js_sandbox_has_no_ambient_access, nullptr},
        {"js.budget_cannot_be_caught", &test_js_budget_cannot_be_caught, nullptr},
        {"js.timeout_and_memory_limits", &test_js_timeout_and_memory_limits, nullptr},
        {"js.assertions_and_locations", &test_js_assertions_and_locations, nullptr},
        {"js.async_tests", &test_js_async_tests, nullptr},
        {"js.suites_hooks_and_tags", &test_js_suites_hooks_and_tags, nullptr},
        {"js.rejects_bad_registration", &test_js_rejects_bad_registration, nullptr},
        {"js.reads_only_granted_files", &test_js_reads_only_granted_files, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
