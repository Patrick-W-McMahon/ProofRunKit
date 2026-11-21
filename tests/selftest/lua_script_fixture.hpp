/**
 * @file lua_script_fixture.hpp
 * @brief Shared self-test fixture for the Lua engine (#9, #12; the host part is shared with #11): loads a script with a
 * fake host (captured print, a scripted clock, in-memory files) and runs the tests it registers.
 */
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cpp/reporters.hpp"
#include "proofrunkit/prk_lua.h"
#include "selftest_support.hpp"

namespace prk::selftest {

using RegistryPointer = std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)>;
using LuaScriptPointer = std::unique_ptr<PrkLuaScript, decltype(&prk_destroy_lua_script)>;

/** @brief State behind the fake PrkScriptHost shared by the Lua and JavaScript fixtures. */
struct FakeScriptHostState {
    std::string printed;                       ///< Everything the script printed.
    std::uint64_t clock_ms = 1000;             ///< Fake monotonic clock.
    std::uint64_t clock_step_ms = 0;           ///< Added to clock_ms on every read.
    std::map<std::string, std::string> files;  ///< Readable files; "secret/denied" paths are refused.
    PrkProcessRunner process_runner{};         ///< Process spawn granted to the script (none by default).
};

/** @brief A loaded script, its fake host, and what loading and running it produced. */
struct LuaScriptRun : FakeScriptHostState {
    RegistryPointer registry{prk_create_registry(), &prk_destroy_registry};
    LuaScriptPointer script{nullptr, &prk_destroy_lua_script};
    PrkStatus load_status = PRK_STATUS_OK;
    std::string load_error;
    std::vector<RecordedTestResult> results;

    /** @brief Names of the registered tests, in order. */
    [[nodiscard]] std::vector<std::string> list_test_names() const {
        std::vector<std::string> names;
        for (std::size_t index = 0; index < prk_count_test_cases(registry.get()); ++index) {
            names.emplace_back(prk_get_test_case_name(registry.get(), index));
        }
        return names;
    }
};

/** @brief Options for load_and_run_lua_script. */
struct LuaScriptRunSetup {
    const PrkLuaLimits* limits = nullptr;
    const PrkScriptRunSettings* settings = nullptr;
    std::uint64_t clock_step_ms = 0;
    std::map<std::string, std::string> files;
    bool grants_host = true;            ///< False creates the script without any host services.
    PrkProcessRunner process_runner{};  ///< Process spawn granted through the host (none by default).
};

namespace fixture_detail {

inline FakeScriptHostState& find_run(void* host_state) {
    return *static_cast<FakeScriptHostState*>(host_state);
}

inline void append_printed_text(void* host_state, const char* text, std::size_t length) {
    find_run(host_state).printed.append(text, length);
}

inline std::uint64_t read_fake_clock(void* host_state) {
    FakeScriptHostState& run = find_run(host_state);
    run.clock_ms += run.clock_step_ms;
    return run.clock_ms;
}

inline PrkFileAccessStatus find_fake_file(void* host_state, const char* path) {
    if (std::string_view(path).starts_with("secret/")) {
        return PRK_FILE_ACCESS_DENIED;
    }
    return find_run(host_state).files.count(path) != 0 ? PRK_FILE_ACCESS_OK : PRK_FILE_ACCESS_NOT_FOUND;
}

inline PrkFileAccessStatus read_fake_file(void* host_state, const char* path, PrkReceiveFileBytes receive_bytes,
                                          void* sink) {
    const PrkFileAccessStatus status = find_fake_file(host_state, path);
    if (status == PRK_FILE_ACCESS_OK) {
        const std::string& bytes = find_run(host_state).files.at(path);
        receive_bytes(sink, bytes.data(), bytes.size());
    }
    return status;
}

inline void record_test_finished(void* state, const PrkTestResult* result) {
    static_cast<std::vector<RecordedTestResult>*>(state)->push_back(record_test_result(*result));
}

/** @brief A PrkScriptHost over @p state (captured print, scripted clock, in-memory files). */
inline PrkScriptHost make_fake_host(FakeScriptHostState& state) {
    return PrkScriptHost{&append_printed_text,
                         &read_fake_clock,
                         &find_fake_file,
                         &read_fake_file,
                         static_cast<FakeScriptHostState*>(&state),
                         state.process_runner,
                         nullptr};
}

}  // namespace fixture_detail

/** @brief Loads @p source as "sample.lua" and, when it loads, runs every test it registered. */
inline std::unique_ptr<LuaScriptRun> load_and_run_lua_script(std::string_view source,
                                                             LuaScriptRunSetup setup = LuaScriptRunSetup{}) {
    auto run = std::make_unique<LuaScriptRun>();
    run->clock_step_ms = setup.clock_step_ms;
    run->files = std::move(setup.files);
    run->process_runner = setup.process_runner;
    const PrkScriptHost host = fixture_detail::make_fake_host(*run);
    run->script.reset(prk_create_lua_script(setup.limits, setup.grants_host ? &host : nullptr, setup.settings));
    std::array<char, PRK_LUA_ERROR_CAPACITY> error{};
    run->load_status = prk_load_lua_test_script(run->script.get(), run->registry.get(), "sample.lua", source.data(),
                                                source.size(), error.data(), error.size());
    run->load_error = error.data();
    if (run->load_status == PRK_STATUS_OK) {
        const PrkReporter reporter{nullptr, &fixture_detail::record_test_finished, nullptr, &run->results};
        (void)prk_run_test_cases(run->registry.get(), nullptr, &reporter, nullptr);
    }
    return run;
}

/** @brief Loads @p source with @p limits (convenience for the sandbox tests). */
inline std::unique_ptr<LuaScriptRun> load_and_run_lua_script(std::string_view source, const PrkLuaLimits* limits) {
    LuaScriptRunSetup setup;
    setup.limits = limits;
    return load_and_run_lua_script(source, std::move(setup));
}

/** @brief Reports whether @p text contains @p expected. */
inline bool contains_text(const std::string& text, std::string_view expected) {
    return text.find(expected) != std::string::npos;
}

}  // namespace prk::selftest
