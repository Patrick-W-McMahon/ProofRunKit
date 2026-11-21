/**
 * @file test_script_loading.cpp
 * @brief Tests for prk::detect_script_language, prk::load_test_scripts, and running scripts through
 *        prk::run_command_line with an in-memory script reader (#9).
 */
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cpp/command_line_app.hpp"
#include "cpp/script_loading.hpp"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

const std::map<std::string, std::string>& get_sample_scripts() {
    static const std::map<std::string, std::string> scripts = {
        {"pass.lua", "print('loaded pass.lua')\nprk.add_test('script.pass', function() prk.expect_true(1 < 2) end)\n"},
        {"fail.lua", "prk.add_test('script.fail', function() prk.expect_equal('a', 'b') end)\n"},
        {"broken.lua", "prk.add_test(\n"},
    };
    return scripts;
}

ScriptFileContent read_sample_script(const std::string& path) {
    const auto& scripts = get_sample_scripts();
    const auto found = scripts.find(path);
    if (found == scripts.end()) {
        return ScriptFileContent{false, {}, "cannot open file"};
    }
    return ScriptFileContent{true, found->second, {}};
}

struct CapturedScriptRun {
    int exit_code;
    std::string output;
    std::string errors;
};

CapturedScriptRun run_scripts_through_command_line(std::vector<std::string_view> arguments) {
    const std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)> builtins(prk_create_registry(),
                                                                                 &prk_destroy_registry);
    std::ostringstream output;
    std::ostringstream errors;
    const CommandLineEnvironment environment{*builtins, PrkClock{nullptr, nullptr}, &read_sample_script, output,
                                             errors};
    const int exit_code = run_command_line("prk", arguments, environment);
    return CapturedScriptRun{exit_code, output.str(), errors.str()};
}

void test_detects_script_language(PrkTestContext* context, void*) {
    PRK_EXPECT(detect_script_language("tests/a.lua") == ScriptLanguage::Lua);
    PRK_EXPECT(detect_script_language("C:\\tests\\A.LUA") == ScriptLanguage::Lua);
    PRK_EXPECT(detect_script_language("suite.js") == ScriptLanguage::JavaScript);
    PRK_EXPECT(detect_script_language("suite.mjs") == ScriptLanguage::JavaScript);
    PRK_EXPECT(detect_script_language("notes.txt") == ScriptLanguage::Unsupported);
    PRK_EXPECT(detect_script_language("dir.lua/script") == ScriptLanguage::Unsupported);
    PRK_EXPECT(detect_script_language("") == ScriptLanguage::Unsupported);
}

void test_loads_scripts_into_one_registry(PrkTestContext* context, void*) {
    const PrkLuaOutput discard{nullptr, nullptr};
    const LoadedTestScripts loaded = load_test_scripts(
        {"pass.lua", "fail.lua", "x.js", "y.txt", "missing.lua", "broken.lua"}, &read_sample_script, discard, nullptr);
    PRK_EXPECT(loaded.registry != nullptr);
    PRK_EXPECT(prk_count_test_cases(loaded.registry.get()) == 2);
    PRK_EXPECT(loaded.unsupported_scripts == std::vector<std::string>({"x.js"}));
    PRK_EXPECT(loaded.unknown_type_scripts == std::vector<std::string>({"y.txt"}));
    PRK_EXPECT(loaded.load_errors.size() == 2);
    PRK_EXPECT(loaded.load_errors[0] == "missing.lua: cannot open file");
    PRK_EXPECT(loaded.load_errors[1].find("broken.lua:2:") == 0);
}

void test_runs_lua_scripts_from_command_line(PrkTestContext* context, void*) {
    const CapturedScriptRun passing = run_scripts_through_command_line({"run", "pass.lua"});
    PRK_EXPECT(passing.exit_code == kExitAllPassed);
    PRK_EXPECT(passing.output.find("loaded pass.lua\n[ PASS ] script.pass") == 0);
    const CapturedScriptRun failing = run_scripts_through_command_line({"pass.lua", "fail.lua"});
    PRK_EXPECT(failing.exit_code == kExitTestFailed);
    PRK_EXPECT(failing.output.find("fail.lua:1: values differ: expected b, got a") != std::string::npos);
    const CapturedScriptRun filtered = run_scripts_through_command_line({"run", "--filter", "pass", "fail.lua"});
    PRK_EXPECT(filtered.exit_code == kExitAllPassed);
}

void test_json_and_list_keep_print_off_output(PrkTestContext* context, void*) {
    const CapturedScriptRun json = run_scripts_through_command_line({"run", "--json", "pass.lua"});
    PRK_EXPECT(json.exit_code == kExitAllPassed && json.output.find("{") == 0);
    PRK_EXPECT(json.errors == "loaded pass.lua\n");
    const CapturedScriptRun listing = run_scripts_through_command_line({"list", "pass.lua", "fail.lua"});
    PRK_EXPECT(listing.output == "script.pass\nscript.fail\n");
}

void test_reports_script_problems_with_exit_codes(PrkTestContext* context, void*) {
    const CapturedScriptRun missing = run_scripts_through_command_line({"run", "missing.lua"});
    PRK_EXPECT(missing.exit_code == kExitScriptError && missing.errors == "prk: missing.lua: cannot open file\n");
    const CapturedScriptRun broken = run_scripts_through_command_line({"run", "broken.lua"});
    PRK_EXPECT(broken.exit_code == kExitScriptError && broken.errors.find("prk: broken.lua:2:") == 0);
    const CapturedScriptRun javascript = run_scripts_through_command_line({"run", "pass.lua", "x.js"});
    PRK_EXPECT(javascript.exit_code == kExitNotImplemented);
    const CapturedScriptRun unknown = run_scripts_through_command_line({"run", "notes.txt"});
    PRK_EXPECT(unknown.exit_code == kExitUsageError);
    PRK_EXPECT(unknown.errors.find("unsupported test script type") != std::string::npos);
}

void test_reads_script_files_from_disk(PrkTestContext* context, void*) {
    const std::string path = "prk_selftest_read_script.lua";
    {
        std::ofstream file(path, std::ios::binary);
        file << "-- sample\r\nreturn 1\n";
    }
    const ScriptFileContent content = read_script_file_from_disk(path);
    (void)std::remove(path.c_str());
    PRK_EXPECT(content.was_read && content.text == "-- sample\r\nreturn 1\n");
    const ScriptFileContent missing = read_script_file_from_disk("no/such/dir/prk_missing.lua");
    PRK_EXPECT(!missing.was_read && missing.error_message == "cannot open file");
}

}  // namespace

PrkStatus register_script_loading_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"scripts.detects_script_language", &test_detects_script_language, nullptr},
        {"scripts.loads_scripts_into_one_registry", &test_loads_scripts_into_one_registry, nullptr},
        {"scripts.runs_lua_scripts_from_command_line", &test_runs_lua_scripts_from_command_line, nullptr},
        {"scripts.json_and_list_keep_print_off_output", &test_json_and_list_keep_print_off_output, nullptr},
        {"scripts.reports_script_problems_with_exit_codes", &test_reports_script_problems_with_exit_codes, nullptr},
        {"scripts.reads_script_files_from_disk", &test_reads_script_files_from_disk, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
