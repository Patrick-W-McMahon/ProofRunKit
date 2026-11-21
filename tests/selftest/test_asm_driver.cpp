/**
 * @file test_asm_driver.cpp
 * @brief Tests for the assembler pass (#16): MASM, GAS, and NASM command lines, family choice by extension,
 *        diagnostics of all three, assembling through a fake runner (including the MASM fallback to MSVC
 *        discovery), prk.assemble in Lua and JavaScript, and a real assemble-link-run of a small function with the
 *        host's assembler (MASM on Windows, GAS elsewhere; skipped when it is missing).
 */
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "drivers/prk_c_driver_internal.h"
#include "lua_script_fixture.hpp"
#include "process/prk_process_internal.h"
#include "proofrunkit/prk_asm_driver.h"
#include "proofrunkit/prk_js.h"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

/** @brief Fake runner: records programs and the last arguments; cmd.exe plays vswhere and vcvarsall. */
struct FakeAssemblerRunner {
    std::set<std::string> missing;
    std::string standard_error;
    int exit_code = 0;
    std::vector<std::string> programs;
    std::vector<std::string> arguments;

    [[nodiscard]] PrkProcessRunner make_runner() { return PrkProcessRunner{&FakeAssemblerRunner::spawn, this}; }

private:
    static char* copy_text(const std::string& text) {
        char* const bytes = static_cast<char*>(std::malloc(text.size() + 1));
        if (bytes != nullptr) {
            std::memcpy(bytes, text.c_str(), text.size() + 1);
        }
        return bytes;
    }

    static PrkStatus spawn(void* state, const PrkProcessRequest* request, PrkProcessResult* out_result) {
        FakeAssemblerRunner& fake = *static_cast<FakeAssemblerRunner*>(state);
        const std::string program = request->executable;
        fake.programs.push_back(program);
        if (fake.missing.count(program) != 0) {
            return prk_set_process_start_failure(("executable not found: " + program).c_str(), out_result);
        }
        fake.arguments.assign(request->arguments, request->arguments + request->argument_count);
        std::string output;
        if (program == "cmd.exe") {
            const bool is_vswhere = fake.arguments.size() > 3 && fake.arguments[3].find("vswhere") != std::string::npos;
            output = is_vswhere ? "C:\\VS\r\n"
                                : "::prk-where::\r\nC:\\VS\\bin\\Hostx64\\x64\\cl.exe\r\n::prk-environment::\r\n"
                                  "Path=C:\\VS\\bin\r\n";
        }
        std::memset(out_result, 0, sizeof *out_result);
        out_result->outcome = PRK_PROCESS_EXITED;
        out_result->exit_code = program == "cmd.exe" ? 0 : fake.exit_code;
        out_result->standard_output = copy_text(output);
        out_result->standard_output_length = output.size();
        out_result->standard_error = copy_text(program == "cmd.exe" ? "" : fake.standard_error);
        out_result->standard_error_length = program == "cmd.exe" ? 0 : fake.standard_error.size();
        return out_result->standard_output != nullptr && out_result->standard_error != nullptr
                   ? PRK_STATUS_OK
                   : PRK_STATUS_OUT_OF_MEMORY;
    }
};

std::vector<std::string> build_arguments(const PrkAssembleRequest& request, PrkAssemblerFamily family) {
    PrkArgumentList list{};
    std::vector<std::string> arguments;
    if (prk_build_assembler_arguments(&request, family, &list) == PRK_STATUS_OK) {
        arguments.assign(list.items, list.items + list.count);
        prk_free_argument_list(&list);
    }
    return arguments;
}

void test_builds_assembler_arguments(PrkTestContext* context, void*) {
    const char* const includes[] = {"inc"};
    const char* const defines[] = {"FAST", "LEVEL=2"};
    const char* const flags[] = {"-g"};
    PrkAssembleRequest request{};
    request.source = "add.asm";
    request.output_path = "add.obj";
    request.include_directories = includes;
    request.include_directory_count = 1;
    request.defines = defines;
    request.define_count = 2;
    request.is_warnings_as_errors = 1;
    PRK_EXPECT(build_arguments(request, PRK_ASSEMBLER_MASM) ==
               std::vector<std::string>(
                   {"/nologo", "/c", "/W3", "/WX", "/Iinc", "/DFAST", "/DLEVEL=2", "/Foadd.obj", "add.asm"}));
    request.object_format = "elf64";
    request.extra_flags = flags;
    request.extra_flag_count = 1;
    PRK_EXPECT(build_arguments(request, PRK_ASSEMBLER_NASM) ==
               std::vector<std::string>(
                   {"-f", "elf64", "-Werror", "-Iinc", "-DFAST", "-DLEVEL=2", "-o", "add.obj", "add.asm", "-g"}));
    request.source = "add.s";
    request.output_path = "add.o";
    PRK_EXPECT(build_arguments(request, PRK_ASSEMBLER_GAS) ==
               std::vector<std::string>({"--fatal-warnings", "-Iinc", "--defsym", "FAST=1", "--defsym", "LEVEL=2", "-o",
                                         "add.o", "add.s", "-g"}));
}

void test_chooses_assembler_family(PrkTestContext* context, void*) {
    PRK_EXPECT(prk_choose_assembler_family(PRK_ASSEMBLER_AUTO, "src/add.s") == PRK_ASSEMBLER_GAS);
    PRK_EXPECT(prk_choose_assembler_family(PRK_ASSEMBLER_AUTO, "add.S") == PRK_ASSEMBLER_GAS);
    PRK_EXPECT(prk_choose_assembler_family(PRK_ASSEMBLER_AUTO, "add.nasm") == PRK_ASSEMBLER_NASM);
#if defined(_WIN32)
    PRK_EXPECT(prk_choose_assembler_family(PRK_ASSEMBLER_AUTO, "add.asm") == PRK_ASSEMBLER_MASM);
#else
    PRK_EXPECT(prk_choose_assembler_family(PRK_ASSEMBLER_AUTO, "add.asm") == PRK_ASSEMBLER_NASM);
#endif
    PRK_EXPECT(prk_choose_assembler_family(PRK_ASSEMBLER_AUTO, "v1.2/add") == PRK_ASSEMBLER_AUTO);
    PRK_EXPECT(prk_choose_assembler_family(PRK_ASSEMBLER_NASM, "add.s") == PRK_ASSEMBLER_NASM);
    PRK_EXPECT(std::strcmp(prk_get_assembler_family_name(PRK_ASSEMBLER_MASM), "masm") == 0);
    PRK_EXPECT(std::strcmp(prk_get_assembler_family_name(PRK_ASSEMBLER_GAS), "gas") == 0);
    PRK_EXPECT(std::strcmp(prk_get_assembler_family_name(PRK_ASSEMBLER_NASM), "nasm") == 0);
    PRK_EXPECT(std::strcmp(prk_get_assembler_family_name(PRK_ASSEMBLER_AUTO), "auto") == 0);
    const std::string format = prk_get_default_nasm_format();
    PRK_EXPECT(format == "win64" || format == "win32" || format == "macho64" || format == "elf64" || format == "elf32");
}

void test_parses_assembler_diagnostics(PrkTestContext* context, void*) {
    PrkCompilerDiagnostic diagnostic{};
    PRK_EXPECT(prk_parse_compiler_diagnostic(PRK_COMPILER_MSVC, "add.asm(2) : error A2008:syntax error : bar",
                                             &diagnostic) == 1);
    PRK_EXPECT(diagnostic.severity == PRK_DIAGNOSTIC_ERROR && diagnostic.line == 2 &&
               std::strcmp(diagnostic.file, "add.asm") == 0 && std::strcmp(diagnostic.code, "A2008") == 0 &&
               std::strcmp(diagnostic.message, "syntax error : bar") == 0);
    PRK_EXPECT(prk_parse_compiler_diagnostic(PRK_COMPILER_GCC, "add.s:3: Error: no such instruction: `foo %rax'",
                                             &diagnostic) == 1);
    PRK_EXPECT(diagnostic.severity == PRK_DIAGNOSTIC_ERROR && diagnostic.line == 3 &&
               std::strcmp(diagnostic.message, "no such instruction: `foo %rax'") == 0);
    PRK_EXPECT(prk_parse_compiler_diagnostic(PRK_COMPILER_GCC, "add.s:4: Warning: end of file not at end of a line",
                                             &diagnostic) == 1);
    PRK_EXPECT(diagnostic.severity == PRK_DIAGNOSTIC_WARNING && diagnostic.line == 4);
    PRK_EXPECT(prk_parse_compiler_diagnostic(PRK_COMPILER_GCC, "add.asm:2: error: parser: instruction expected",
                                             &diagnostic) == 1);
    PRK_EXPECT(diagnostic.line == 2 && std::strcmp(diagnostic.message, "parser: instruction expected") == 0);
    PRK_EXPECT(prk_parse_compiler_diagnostic(PRK_COMPILER_GCC, "add.s: Assembler messages:", &diagnostic) == 0);
}

void test_assembles_through_runner(PrkTestContext* context, void*) {
    FakeAssemblerRunner fake;
    fake.exit_code = 1;
    fake.standard_error = "add.s: Assembler messages:\nadd.s:2: Error: no such instruction: `foo'\n";
    const PrkProcessRunner runner = fake.make_runner();
    PrkAssembleRequest request{};
    request.source = "add.s";
    request.output_path = "add.o";
    PrkCompileResult result{};
    PRK_EXPECT(prk_assemble_source(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(fake.programs == std::vector<std::string>({"as"}) && result.kind == PRK_OUTPUT_OBJECT);
    PRK_EXPECT(result.process.exit_code == 1 && result.error_count == 1 && result.diagnostic_count == 1);
    PRK_EXPECT(result.command_line != nullptr && std::strcmp(result.command_line, "as -o add.o add.s") == 0);
    prk_release_compile_result(&result);
    request.source = "add.inc";
    PRK_EXPECT(prk_assemble_source(&runner, &request, &result) == PRK_STATUS_INVALID_ARGUMENT);
    request.source = "add.s";
    request.assembler_program = "x86_64-w64-mingw32-as";
    PRK_EXPECT(prk_assemble_source(&runner, &request, &result) == PRK_STATUS_INVALID_ARGUMENT);
    request.family = PRK_ASSEMBLER_GAS;
    PRK_EXPECT(prk_assemble_source(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(std::strcmp(result.compiler_program, "x86_64-w64-mingw32-as") == 0);
    prk_release_compile_result(&result);
    PRK_EXPECT(prk_assemble_source(nullptr, &request, &result) == PRK_STATUS_INVALID_ARGUMENT);
}

void test_masm_falls_back_to_discovery(PrkTestContext* context, void*) {
    FakeAssemblerRunner fake;
    fake.missing = {"ml64", "ml"};
    const PrkProcessRunner runner = fake.make_runner();
    PrkMsvcToolchain cache{};
    PrkAssembleRequest request{};
    request.family = PRK_ASSEMBLER_MASM;
    request.source = "add.asm";
    request.output_path = "add.obj";
    request.msvc_architecture = "x64";
    request.msvc_toolchain = &cache;
    PrkCompileResult result{};
    PRK_EXPECT(prk_assemble_source(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(fake.programs ==
               std::vector<std::string>({"ml64", "cmd.exe", "cmd.exe", "C:\\VS\\bin\\Hostx64\\x64\\ml64.exe"}));
    PRK_EXPECT(result.is_msvc_discovered == 1 && result.family == PRK_COMPILER_MSVC);
    prk_release_compile_result(&result);
    fake.programs.clear();
    request.msvc_architecture = "x64_x86";
    PRK_EXPECT(prk_assemble_source(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(fake.programs == std::vector<std::string>({"C:\\VS\\bin\\Hostx64\\x64\\ml.exe"}));
    prk_release_compile_result(&result);
    prk_release_msvc_toolchain(&cache);
    fake.missing.insert("cmd.exe");
    request.msvc_toolchain = nullptr;
    PRK_EXPECT(prk_assemble_source(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(result.process.outcome == PRK_PROCESS_FAILED_TO_START &&
               std::strstr(result.process.error_message, "ml is not on PATH and MSVC discovery failed") != nullptr);
    prk_release_compile_result(&result);
}

void test_lua_and_js_assemble(PrkTestContext* context, void*) {
    FakeAssemblerRunner fake;
    LuaScriptRunSetup setup;
    setup.process_runner = fake.make_runner();
    const auto lua = load_and_run_lua_script(
        "prk.add_test('nasm', function()\n"
        "  local r = prk.assemble{source = 'add.nasm', output = 'add.o', format = 'elf64', defines = 'X'}\n"
        "  prk.expect_exit_code(r, 0) prk.expect_equal(r.assembler, 'nasm') prk.expect_nil(r.compiler)\n"
        "  prk.expect_equal(r.kind, 'object') prk.expect_equal(r.command, 'nasm -f elf64 -DX -o add.o add.nasm')\n"
        "end)\n"
        "prk.add_test('unknown', function() prk.assemble{source = 'add.inc', output = 'a.o'} end)\n"
        "prk.add_test('typo', function() prk.assemble{source = 'a.s', output = 'a.o', assembler = 'gas', fromat = 1} "
        "end)\n",
        std::move(setup));
    PRK_EXPECT(lua->results.size() == 3 && lua->results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(lua->results.size() == 3 && contains_text(lua->results[1].message, "unknown source extension"));
    PRK_EXPECT(lua->results.size() == 3 &&
               contains_text(lua->results[2].message, "prk.assemble: unknown option 'fromat'"));
    FakeScriptHostState js_host;
    js_host.process_runner = fake.make_runner();
    const PrkScriptHost host = fixture_detail::make_fake_host(js_host);
    RegistryPointer registry{prk_create_registry(), &prk_destroy_registry};
    std::unique_ptr<PrkJsScript, decltype(&prk_destroy_js_script)> script{prk_create_js_script(nullptr, &host, nullptr),
                                                                          &prk_destroy_js_script};
    const std::string source =
        "prk.addTest('gas', () => {\n"
        "  const r = prk.assemble({source: 'a.s', output: 'a.o', werror: true});\n"
        "  prk.expectExitCode(r, 0); prk.expectEqual(r.assembler, 'gas');\n"
        "  prk.expectEqual(r.compiler, undefined); prk.expectEqual(r.program, 'as');\n"
        "});\n"
        "prk.addTest('arch', () => { prk.assemble({source: 'a.asm', output: 'a', msvcArch: 'x'}); });\n";
    std::array<char, PRK_SCRIPT_ERROR_CAPACITY> error{};
    std::vector<RecordedTestResult> results;
    PRK_EXPECT(prk_load_js_test_script(script.get(), registry.get(), "asm.js", source.data(), source.size(),
                                       error.data(), error.size()) == PRK_STATUS_OK);
    const PrkReporter reporter{nullptr, &fixture_detail::record_test_finished, nullptr, &results};
    (void)prk_run_test_cases(registry.get(), nullptr, &reporter, nullptr);
    PRK_EXPECT(results.size() == 2 && results[0].message.empty());
    PRK_EXPECT(results.size() == 2 && contains_text(results[1].message, "msvcArch must be"));
    PRK_EXPECT(fake.arguments == std::vector<std::string>({"--fatal-warnings", "-o", "a.o", "a.s"}));
}

/** @brief Writes @p text to @p path. @return true on success. */
bool write_text_file(const std::string& path, const char* text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    return static_cast<bool>(file);
}

#if defined(_WIN32) && (defined(_WIN64) || defined(__x86_64__))
constexpr const char* real_source_name = "prk_asm_selftest.asm";
constexpr const char* real_source =
    ".code\nprk_add_numbers PROC\n  lea eax, [rcx+rdx]\n  ret\nprk_add_numbers ENDP\nEND\n";
#elif defined(_WIN32)
constexpr const char* real_source_name = "prk_asm_selftest.asm";
constexpr const char* real_source =
    ".686\n.model flat, c\n.code\nprk_add_numbers PROC lhs:DWORD, rhs:DWORD\n"
    "  mov eax, lhs\n  add eax, rhs\n  ret\nprk_add_numbers ENDP\nEND\n";
#else
constexpr const char* real_source_name = "prk_asm_selftest.s";
constexpr const char* real_source =
    ".text\n.globl prk_add_numbers\nprk_add_numbers:\n  leal (%rdi,%rsi), %eax\n  ret\n"
    ".section .note.GNU-stack,\"\",@progbits\n";
#endif

/** @brief Assembles real_source, links it with a C main, and runs it; sets @p out_reason when there is no assembler. */
void assemble_link_and_run(PrkTestContext* context, PrkMsvcToolchain* toolchain, std::string* out_reason) {
    const PrkProcessRunner runner = prk_get_system_process_runner();
    PrkAssembleRequest request{};
    request.source = real_source_name;
    request.output_path = "prk_asm_selftest_object.obj";
    request.msvc_toolchain = toolchain;
    PrkCompileResult assembled{};
    PRK_EXPECT(prk_assemble_source(&runner, &request, &assembled) == PRK_STATUS_OK);
    const bool is_missing = assembled.process.outcome == PRK_PROCESS_FAILED_TO_START;
    if (is_missing) {
        *out_reason = std::string("no assembler: ") + assembled.process.error_message;
    }
    PRK_EXPECT(is_missing || (assembled.process.outcome == PRK_PROCESS_EXITED && assembled.process.exit_code == 0));
    prk_release_compile_result(&assembled);
    if (is_missing) {
        return;
    }
    const char* const sources[] = {"prk_asm_selftest_main.c", "prk_asm_selftest_object.obj"};
    PrkCompileRequest link{};
    link.family = toolchain != nullptr && toolchain->is_found ? PRK_COMPILER_MSVC : PRK_COMPILER_AUTO;
    link.sources = sources;
    link.source_count = 2;
    link.output_path = "prk_asm_selftest_program";
    link.msvc_toolchain = toolchain;
    PrkCompileResult linked{};
    PRK_EXPECT(prk_compile_program(&runner, &link, &linked) == PRK_STATUS_OK);
    PRK_EXPECT(linked.process.outcome == PRK_PROCESS_EXITED && linked.process.exit_code == 0);
    PrkProcessRequest run{};
#if defined(_WIN32)
    const std::string program = std::string(".\\") + linked.output_path;
#else
    const std::string program = std::string("./") + linked.output_path;
#endif
    run.executable = program.c_str();
    run.is_environment_inherited = 1;
    PrkProcessResult ran{};
    PRK_EXPECT(prk_spawn_system_process(nullptr, &run, &ran) == PRK_STATUS_OK);
    PRK_EXPECT(ran.outcome == PRK_PROCESS_EXITED && ran.exit_code == 0);
    prk_release_process_result(&ran);
    (void)std::remove(linked.output_path);
    prk_release_compile_result(&linked);
}

void test_assembles_links_and_runs(PrkTestContext* context, void*) {
    if (std::strcmp(prk_get_native_msvc_architecture(), "arm64") == 0) {
        prk_skip_test(context, "the sample is x86/x64 assembly");
        return;
    }
    PRK_EXPECT(write_text_file(real_source_name, real_source));
    PRK_EXPECT(write_text_file("prk_asm_selftest_main.c",
                               "int prk_add_numbers(int first, int second);\n"
                               "int main(void) { return prk_add_numbers(2, 3) == 5 ? 0 : 1; }\n"));
    PrkMsvcToolchain toolchain{};
    std::string reason;
    assemble_link_and_run(context, &toolchain, &reason);
    prk_release_msvc_toolchain(&toolchain);
    for (const char* path :
         {real_source_name, "prk_asm_selftest_main.c", "prk_asm_selftest_object.obj", "prk_asm_selftest_main.obj"}) {
        (void)std::remove(path);
    }
    if (!reason.empty()) {
        prk_skip_test(context, reason.c_str());
    }
}

}  // namespace

PrkStatus register_asm_driver_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"asm_driver.builds_assembler_arguments", &test_builds_assembler_arguments, nullptr},
        {"asm_driver.chooses_family_by_extension", &test_chooses_assembler_family, nullptr},
        {"asm_driver.parses_assembler_diagnostics", &test_parses_assembler_diagnostics, nullptr},
        {"asm_driver.assembles_through_runner", &test_assembles_through_runner, nullptr},
        {"asm_driver.masm_falls_back_to_discovery", &test_masm_falls_back_to_discovery, nullptr},
        {"asm_driver.lua_and_js_assemble", &test_lua_and_js_assemble, nullptr},
        {"asm_driver.assembles_links_and_runs", &test_assembles_links_and_runs, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
