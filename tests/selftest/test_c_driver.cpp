/**
 * @file test_c_driver.cpp
 * @brief Tests for the C/C++ compiler driver (#14): GCC/Clang and MSVC diagnostics parsing, command lines,
 *        output paths, compiles through a fake runner (including AUTO fallback), prk.compile in Lua and
 *        JavaScript, and one real compile when a compiler is on PATH (skipped otherwise).
 */
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "drivers/prk_c_driver_internal.h"
#include "lua_script_fixture.hpp"
#include "process/prk_process_internal.h"
#include "proofrunkit/prk_c_driver.h"
#include "proofrunkit/prk_js.h"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

/** @brief Fake runner: programs in @p missing fail to start; others exit with a canned code and stderr. */
struct FakeCompilerRunner {
    std::set<std::string> missing;
    int exit_code = 0;
    std::string standard_error;
    std::vector<std::string> programs;
    std::vector<std::string> arguments;  ///< Arguments of the last spawn.

    [[nodiscard]] PrkProcessRunner make_runner() { return PrkProcessRunner{&FakeCompilerRunner::spawn, this}; }

private:
    static PrkStatus spawn(void* state, const PrkProcessRequest* request, PrkProcessResult* out_result) {
        FakeCompilerRunner& fake = *static_cast<FakeCompilerRunner*>(state);
        fake.programs.emplace_back(request->executable);
        fake.arguments.assign(request->arguments, request->arguments + request->argument_count);
        if (fake.missing.count(request->executable) != 0) {
            const std::string message = std::string("executable not found: ") + request->executable;
            return prk_set_process_start_failure(message.c_str(), out_result);
        }
        std::memset(out_result, 0, sizeof *out_result);
        out_result->outcome = PRK_PROCESS_EXITED;
        out_result->exit_code = fake.exit_code;
        out_result->standard_output = static_cast<char*>(std::calloc(1, 1));
        out_result->standard_error = static_cast<char*>(std::malloc(fake.standard_error.size() + 1));
        if (out_result->standard_output == nullptr || out_result->standard_error == nullptr) {
            prk_release_process_result(out_result);
            return PRK_STATUS_OUT_OF_MEMORY;
        }
        std::memcpy(out_result->standard_error, fake.standard_error.c_str(), fake.standard_error.size() + 1);
        out_result->standard_error_length = fake.standard_error.size();
        return PRK_STATUS_OK;
    }
};

struct ParsedLine {
    int is_parsed = 0;
    PrkCompilerDiagnostic diagnostic{};
};

ParsedLine parse_line(PrkCompilerFamily family, const char* line) {
    ParsedLine parsed;
    parsed.is_parsed = prk_parse_compiler_diagnostic(family, line, &parsed.diagnostic);
    return parsed;
}

bool is_diagnostic(const ParsedLine& parsed, PrkDiagnosticSeverity severity, const char* file, unsigned line,
                   unsigned column, const char* code, const char* message) {
    const PrkCompilerDiagnostic& d = parsed.diagnostic;
    return parsed.is_parsed == 1 && d.severity == severity && std::strcmp(d.file, file) == 0 && d.line == line &&
           d.column == column && std::strcmp(d.code, code) == 0 && std::strcmp(d.message, message) == 0;
}

void test_parses_gcc_diagnostics(PrkTestContext* context, void*) {
    PRK_EXPECT(is_diagnostic(parse_line(PRK_COMPILER_GCC, "src/a.c:12:5: error: 'x' undeclared (first use)"),
                             PRK_DIAGNOSTIC_ERROR, "src/a.c", 12, 5, "", "'x' undeclared (first use)"));
    PRK_EXPECT(
        is_diagnostic(parse_line(PRK_COMPILER_CLANG, "a.c:3:7: warning: unused variable 'y' [-Wunused-variable]"),
                      PRK_DIAGNOSTIC_WARNING, "a.c", 3, 7, "-Wunused-variable", "unused variable 'y'"));
    PRK_EXPECT(is_diagnostic(parse_line(PRK_COMPILER_GCC, "C:\\work\\b.c:9: note: declared here"), PRK_DIAGNOSTIC_NOTE,
                             "C:\\work\\b.c", 9, 0, "", "declared here"));
    PRK_EXPECT(is_diagnostic(parse_line(PRK_COMPILER_GCC, "cc1: fatal error: nope.c: No such file or directory"),
                             PRK_DIAGNOSTIC_ERROR, "cc1", 0, 0, "", "nope.c: No such file or directory"));
    PRK_EXPECT(is_diagnostic(parse_line(PRK_COMPILER_GCC, "collect2: error: ld returned 1 exit status"),
                             PRK_DIAGNOSTIC_ERROR, "collect2", 0, 0, "", "ld returned 1 exit status"));
    PRK_EXPECT(parse_line(PRK_COMPILER_GCC, "In function 'main':").is_parsed == 0);
    PRK_EXPECT(parse_line(PRK_COMPILER_GCC, "    5 |   int y;").is_parsed == 0);
    PRK_EXPECT(parse_line(PRK_COMPILER_GCC, ": error: no file").is_parsed == 0);
    PRK_EXPECT(prk_parse_compiler_diagnostic(PRK_COMPILER_GCC, nullptr, nullptr) == 0);
}

void test_parses_msvc_diagnostics(PrkTestContext* context, void*) {
    PRK_EXPECT(is_diagnostic(parse_line(PRK_COMPILER_MSVC, "src\\a.c(12): error C2065: 'x': undeclared identifier"),
                             PRK_DIAGNOSTIC_ERROR, "src\\a.c", 12, 0, "C2065", "'x': undeclared identifier"));
    PRK_EXPECT(is_diagnostic(
        parse_line(PRK_COMPILER_MSVC, "C:\\w\\b.cpp(3,10): warning C4100: 'p': unreferenced formal parameter"),
        PRK_DIAGNOSTIC_WARNING, "C:\\w\\b.cpp", 3, 10, "C4100", "'p': unreferenced formal parameter"));
    PRK_EXPECT(is_diagnostic(parse_line(PRK_COMPILER_MSVC, "LINK : fatal error LNK1561: entry point must be defined"),
                             PRK_DIAGNOSTIC_ERROR, "LINK", 0, 0, "LNK1561", "entry point must be defined"));
    PRK_EXPECT(
        is_diagnostic(parse_line(PRK_COMPILER_MSVC, "cl : command line warning D9002: ignoring unknown option '/foo'"),
                      PRK_DIAGNOSTIC_WARNING, "cl", 0, 0, "D9002", "ignoring unknown option '/foo'"));
    PRK_EXPECT(is_diagnostic(parse_line(PRK_COMPILER_MSVC, "a.c(5): note: see declaration of 'x'"), PRK_DIAGNOSTIC_NOTE,
                             "a.c", 5, 0, "", "see declaration of 'x'"));
    PRK_EXPECT(is_diagnostic(parse_line(PRK_COMPILER_MSVC, "f(x).c(8): error C3861: 'g(': identifier not found"),
                             PRK_DIAGNOSTIC_ERROR, "f(x).c", 8, 0, "C3861", "'g(': identifier not found"));
    PRK_EXPECT(parse_line(PRK_COMPILER_MSVC, "a.c").is_parsed == 0);
    PRK_EXPECT(parse_line(PRK_COMPILER_MSVC, "Microsoft (R) C/C++ Optimizing Compiler").is_parsed == 0);
}

std::vector<std::string> build_arguments(const PrkCompileRequest& request, PrkCompilerFamily family) {
    PrkArgumentList list{};
    std::vector<std::string> arguments;
    if (prk_build_compiler_arguments(&request, family, "out/app", &list) == PRK_STATUS_OK) {
        arguments.assign(list.items, list.items + list.count);
        prk_free_argument_list(&list);
    }
    return arguments;
}

void test_builds_compiler_arguments(PrkTestContext* context, void*) {
    const char* const sources[] = {"a.c", "b c.c"};
    const char* const includes[] = {"inc"};
    const char* const defines[] = {"NDEBUG", "LEVEL=2"};
    const char* const flags[] = {"-O2"};
    PrkCompileRequest request{};
    request.sources = sources;
    request.source_count = 2;
    request.output_path = "out/app";
    request.standard = "c17";
    request.include_directories = includes;
    request.include_directory_count = 1;
    request.defines = defines;
    request.define_count = 2;
    request.extra_flags = flags;
    request.extra_flag_count = 1;
    request.is_warnings_as_errors = 1;
    PRK_EXPECT(build_arguments(request, PRK_COMPILER_GCC) ==
               std::vector<std::string>({"-fdiagnostics-color=never", "-Wall", "-Wextra", "-Werror", "-std=c17",
                                         "-Iinc", "-DNDEBUG", "-DLEVEL=2", "a.c", "b c.c", "-o", "out/app", "-O2"}));
    request.language = PRK_SOURCE_LANGUAGE_CXX;
    request.standard = "c++20";
    request.is_warnings_as_errors = 0;
    request.extra_flag_count = 0;
    PRK_EXPECT(build_arguments(request, PRK_COMPILER_MSVC) ==
               std::vector<std::string>({"/nologo", "/W4", "/EHsc", "/std:c++20", "/Iinc", "/DNDEBUG", "/DLEVEL=2",
                                         "a.c", "b c.c", "/Feout/app"}));
}

void test_formats_output_path(PrkTestContext* context, void*) {
    std::array<char, 64> path{};
    prk_format_compile_output_path("build/hello", path.data(), path.size());
#if defined(_WIN32)
    PRK_EXPECT(std::strcmp(path.data(), "build/hello.exe") == 0);
    prk_format_compile_output_path("v1.2\\hello", path.data(), path.size());
    PRK_EXPECT(std::strcmp(path.data(), "v1.2\\hello.exe") == 0);
#else
    PRK_EXPECT(std::strcmp(path.data(), "build/hello") == 0);
#endif
    prk_format_compile_output_path("build/hello.out", path.data(), path.size());
    PRK_EXPECT(std::strcmp(path.data(), "build/hello.out") == 0);
}

void test_names_families_and_programs(PrkTestContext* context, void*) {
    PRK_EXPECT(std::strcmp(prk_get_compiler_family_name(PRK_COMPILER_GCC), "gcc") == 0);
    PRK_EXPECT(std::strcmp(prk_get_compiler_family_name(PRK_COMPILER_MSVC), "msvc") == 0);
    PRK_EXPECT(std::strcmp(prk_get_compiler_family_name(PRK_COMPILER_AUTO), "auto") == 0);
    PRK_EXPECT(std::strcmp(prk_get_default_compiler_program(PRK_COMPILER_GCC, PRK_SOURCE_LANGUAGE_CXX), "g++") == 0);
    PRK_EXPECT(std::strcmp(prk_get_default_compiler_program(PRK_COMPILER_CLANG, PRK_SOURCE_LANGUAGE_C), "clang") == 0);
    PRK_EXPECT(std::strcmp(prk_get_default_compiler_program(PRK_COMPILER_MSVC, PRK_SOURCE_LANGUAGE_CXX), "cl") == 0);
    PRK_EXPECT(prk_get_default_compiler_program(PRK_COMPILER_AUTO, PRK_SOURCE_LANGUAGE_C) == nullptr);
}

PrkCompileRequest make_hello_request(const char* const* sources) {
    PrkCompileRequest request{};
    request.sources = sources;
    request.source_count = 1;
    request.output_path = "hello.out";
    return request;
}

void test_compiles_through_runner(PrkTestContext* context, void*) {
    FakeCompilerRunner fake;
    fake.exit_code = 1;
    fake.standard_error =
        "hello.cpp: In function 'int main()':\n"
        "hello.cpp:2:9: warning: unused variable 'y' [-Wunused-variable]\r\n"
        "hello.cpp:3:1: error: expected ';' before '}' token\n";
    const PrkProcessRunner runner = fake.make_runner();
    const char* const sources[] = {"hello.cpp"};
    PrkCompileRequest request = make_hello_request(sources);
    request.family = PRK_COMPILER_GCC;
    request.language = PRK_SOURCE_LANGUAGE_CXX;
    PrkCompileResult result{};
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(fake.programs == std::vector<std::string>({"g++"}));
    PRK_EXPECT(result.process.outcome == PRK_PROCESS_EXITED && result.process.exit_code == 1);
    PRK_EXPECT(result.diagnostic_count == 2 && result.error_count == 1 && result.warning_count == 1);
    PRK_EXPECT(result.diagnostic_count == 2 && std::strcmp(result.diagnostics[0].code, "-Wunused-variable") == 0);
    PRK_EXPECT(result.diagnostic_count == 2 && result.diagnostics[1].line == 3);
    PRK_EXPECT(result.command_line != nullptr &&
               std::strcmp(result.command_line, "g++ -fdiagnostics-color=never -Wall -Wextra hello.cpp -o hello.out") ==
                   0);
    PRK_EXPECT(std::strcmp(result.compiler_program, "g++") == 0 && std::strcmp(result.output_path, "hello.out") == 0);
    prk_release_compile_result(&result);
    PRK_EXPECT(result.command_line == nullptr && result.diagnostics == nullptr);
    prk_release_compile_result(nullptr);
}

void test_auto_falls_back_when_compiler_missing(PrkTestContext* context, void*) {
    FakeCompilerRunner fake;
#if defined(_WIN32)
    fake.missing = {"cl"};
    const std::vector<std::string> first_order = {"cl", "clang"};
    const std::vector<std::string> full_order = {"cl", "clang", "gcc"};
#else
    fake.missing = {"gcc"};
    const std::vector<std::string> first_order = {"gcc", "clang"};
    const std::vector<std::string> full_order = {"gcc", "clang"};
#endif
    const PrkProcessRunner runner = fake.make_runner();
    const char* const sources[] = {"hello.c"};
    const PrkCompileRequest request = make_hello_request(sources);
    PrkCompileResult result{};
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(fake.programs == first_order && result.family == PRK_COMPILER_CLANG);
    PRK_EXPECT(result.process.outcome == PRK_PROCESS_EXITED);
    prk_release_compile_result(&result);
    fake.programs.clear();
    fake.missing = {"cl", "clang", "gcc"};
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(fake.programs == full_order && result.process.outcome == PRK_PROCESS_FAILED_TO_START);
    PRK_EXPECT(std::strstr(result.process.error_message, "no C/C++ compiler found on PATH (tried ") != nullptr);
    prk_release_compile_result(&result);
}

void test_rejects_invalid_compile_requests(PrkTestContext* context, void*) {
    FakeCompilerRunner fake;
    const PrkProcessRunner runner = fake.make_runner();
    const char* const sources[] = {"hello.c"};
    PrkCompileRequest request = make_hello_request(sources);
    PrkCompileResult result{};
    PRK_EXPECT(prk_compile_program(nullptr, &request, &result) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_compile_program(&runner, &request, nullptr) == PRK_STATUS_INVALID_ARGUMENT);
    request.compiler_program = "x86_64-w64-mingw32-gcc"; /* A program override needs a family. */
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_INVALID_ARGUMENT);
    request.compiler_program = nullptr;
    request.source_count = 0;
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_INVALID_ARGUMENT);
    request.source_count = 1;
    request.output_path = "";
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(fake.programs.empty());
}

void test_lua_compile_binding(PrkTestContext* context, void*) {
    FakeCompilerRunner fake;
    fake.exit_code = 1;
    fake.standard_error = "a.c:4:2: error: boom\n";
    LuaScriptRunSetup setup;
    setup.process_runner = fake.make_runner();
    const auto run = load_and_run_lua_script(
        "prk.add_test('compile', function()\n"
        "  local r = prk.compile{sources = 'a.c', output = 'a.out', compiler = 'clang', program = 'clang-18',\n"
        "    std = 'c17', defines = {'X=1'}, werror = true}\n"
        "  prk.expect_false(r.ok) prk.expect_equal(r.exit_code, 1) prk.expect_equal(r.compiler, 'clang')\n"
        "  prk.expect_equal(r.program, 'clang-18') prk.expect_equal(r.errors, 1) prk.expect_equal(r.warnings, 0)\n"
        "  prk.expect_equal(#r.diagnostics, 1) prk.expect_equal(r.diagnostics[1].severity, 'error')\n"
        "  prk.expect_equal(r.diagnostics[1].line, 4) prk.expect_equal(r.diagnostics[1].message, 'boom')\n"
        "  prk.expect_equal(r.output, 'a.out') prk.expect_match(r.command, '^clang%-18 ')\n"
        "end)\n"
        "prk.add_test('bad', function() prk.compile{sources = 'a.c', output = 'a', compilr = 'gcc'} end)\n"
        "prk.add_test('auto_program', function() prk.compile{sources = 'a.c', output = 'a', program = 'x'} end)\n",
        std::move(setup));
    PRK_EXPECT(run->results.size() == 3 && run->results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(run->results.size() == 3 && contains_text(run->results[1].message, "unknown option 'compilr'"));
    PRK_EXPECT(run->results.size() == 3 && contains_text(run->results[2].message, "program needs compiler"));
    PRK_EXPECT(fake.arguments == std::vector<std::string>({"-fdiagnostics-color=never", "-Wall", "-Wextra", "-Werror",
                                                           "-std=c17", "-DX=1", "a.c", "-o", "a.out"}));
}

/** @brief Results of a JavaScript script run with a fake host. */
struct JsCompileRun : FakeScriptHostState {
    RegistryPointer registry{prk_create_registry(), &prk_destroy_registry};
    std::unique_ptr<PrkJsScript, decltype(&prk_destroy_js_script)> script{nullptr, &prk_destroy_js_script};
    std::vector<RecordedTestResult> results;
};

std::unique_ptr<JsCompileRun> load_and_run_js(std::string_view source, const PrkProcessRunner& runner) {
    auto run = std::make_unique<JsCompileRun>();
    run->process_runner = runner;
    const PrkScriptHost host = fixture_detail::make_fake_host(*run);
    run->script.reset(prk_create_js_script(nullptr, &host, nullptr));
    std::array<char, PRK_SCRIPT_ERROR_CAPACITY> error{};
    if (prk_load_js_test_script(run->script.get(), run->registry.get(), "compile.js", source.data(), source.size(),
                                error.data(), error.size()) == PRK_STATUS_OK) {
        const PrkReporter reporter{nullptr, &fixture_detail::record_test_finished, nullptr, &run->results};
        (void)prk_run_test_cases(run->registry.get(), nullptr, &reporter, nullptr);
    }
    return run;
}

void test_js_compile_binding(PrkTestContext* context, void*) {
    FakeCompilerRunner fake;
    fake.standard_error = "w.cpp(7,3): warning C4189: 'v': local variable is initialized but not referenced\n";
    const auto run = load_and_run_js(
        "prk.addTest('compile', () => {\n"
        "  const r = prk.compile({sources: ['w.cpp'], output: 'w.exe', language: 'c++', compiler: 'msvc',\n"
        "    includes: 'inc', flags: ['/O2'], timeoutMs: 5000});\n"
        "  prk.expectExitCode(r, 0); prk.expectTrue(r.ok); prk.expectEqual(r.warnings, 1);\n"
        "  prk.expectEqual(r.diagnostics[0].code, 'C4189'); prk.expectEqual(r.diagnostics[0].column, 3);\n"
        "  prk.expectEqual(r.compiler, 'msvc'); prk.expectEqual(r.program, 'cl');\n"
        "});\n"
        "prk.addTest('bad', () => { prk.compile({sources: [], output: 'a'}); });\n"
        "prk.addTest('lang', () => { prk.compile({sources: 'a.c', output: 'a', language: 'cobol'}); });\n",
        fake.make_runner());
    PRK_EXPECT(run->results.size() == 3 && run->results[0].message.empty());
    PRK_EXPECT(run->results.size() == 3 && contains_text(run->results[1].message, "at least one source"));
    PRK_EXPECT(run->results.size() == 3 && contains_text(run->results[2].message, "unknown language 'cobol'"));
    PRK_EXPECT(fake.arguments ==
               std::vector<std::string>({"/nologo", "/W4", "/EHsc", "/Iinc", "w.cpp", "/Few.exe", "/O2"}));
}

void test_compiles_real_program(PrkTestContext* context, void*) {
    const std::string source_path = "prk_c_driver_selftest.c";
    std::FILE* const file = std::fopen(source_path.c_str(), "wb");
    if (file == nullptr) {
        prk_fail_test(context, "cannot write the sample source file");
        return;
    }
    (void)std::fputs("int main(void) { int unused_value = 0; return 0; }\n", file);
    (void)std::fclose(file);
    const char* const sources[] = {source_path.c_str()};
    PrkCompileRequest request = make_hello_request(sources);
    request.output_path = "prk_c_driver_selftest_program";
    PrkCompileResult result{};
    const PrkProcessRunner runner = prk_get_system_process_runner();
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_OK);
    const bool is_compiler_missing = result.process.outcome == PRK_PROCESS_FAILED_TO_START;
    if (!is_compiler_missing) {
        PRK_EXPECT(result.process.outcome == PRK_PROCESS_EXITED && result.process.exit_code == 0);
        PRK_EXPECT(result.warning_count == 1 && result.error_count == 0);
        (void)std::remove(result.output_path);
    }
    prk_release_compile_result(&result);
    (void)std::remove(source_path.c_str());
    (void)std::remove("prk_c_driver_selftest.obj"); /* MSVC leaves the object file in the working directory. */
    if (is_compiler_missing) {
        prk_skip_test(context, "no C compiler on PATH");
    }
}

}  // namespace

PrkStatus register_c_driver_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"c_driver.parses_gcc_diagnostics", &test_parses_gcc_diagnostics, nullptr},
        {"c_driver.parses_msvc_diagnostics", &test_parses_msvc_diagnostics, nullptr},
        {"c_driver.builds_compiler_arguments", &test_builds_compiler_arguments, nullptr},
        {"c_driver.formats_output_path", &test_formats_output_path, nullptr},
        {"c_driver.names_families_and_programs", &test_names_families_and_programs, nullptr},
        {"c_driver.compiles_through_runner", &test_compiles_through_runner, nullptr},
        {"c_driver.auto_falls_back_when_compiler_missing", &test_auto_falls_back_when_compiler_missing, nullptr},
        {"c_driver.rejects_invalid_requests", &test_rejects_invalid_compile_requests, nullptr},
        {"c_driver.lua_compile_binding", &test_lua_compile_binding, nullptr},
        {"c_driver.js_compile_binding", &test_js_compile_binding, nullptr},
        {"c_driver.compiles_real_program", &test_compiles_real_program, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
