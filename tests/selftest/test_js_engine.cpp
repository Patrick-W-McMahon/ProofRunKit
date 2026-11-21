/**
 * @file test_js_engine.cpp
 * @brief Internal tests for the embedded QuickJS-ng engine (#11): instruction/time/memory budgets that
 *        try/catch cannot swallow, with custom limits.
 *        Cases that prk scripts cover are in prk.tests.json (docs/self-hosting.md, #96).
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

}  // namespace

PrkStatus register_js_engine_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"js.budget_cannot_be_caught", &test_js_budget_cannot_be_caught, nullptr},
        {"js.timeout_and_memory_limits", &test_js_timeout_and_memory_limits, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
