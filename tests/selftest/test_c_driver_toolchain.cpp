/**
 * @file test_c_driver_toolchain.cpp
 * @brief Tests for the rest of the C/C++ driver (#14): object, static-library, and shared-library command
 *        lines, library options, MSVC discovery (vswhere and vcvarsall parsers, discovery and PATH fallback
 *        through a fake runner, the discovery cache), and a real MSVC discovery on Windows when Visual Studio is
 *        installed (skipped otherwise).
 */
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "drivers/prk_c_driver_internal.h"
#include "lua_script_fixture.hpp"
#include "process/prk_process_internal.h"
#include "proofrunkit/prk_c_driver.h"
#include "proofrunkit/prk_msvc_toolchain.h"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

constexpr const char* fake_cl_path = "C:\\VS\\VC\\Tools\\MSVC\\14.44\\bin\\Hostx64\\x64\\cl.exe";

/** @brief Encodes ASCII (or Latin-1) @p text as UTF-16LE, like `cmd.exe /u`. */
std::string encode_utf16le(const std::string& text) {
    std::string wide;
    for (const char character : text) {
        wide.push_back(character);
        wide.push_back('\0');
    }
    return wide;
}

/** @brief Canned `set` listing after vcvarsall, as 8-bit text. */
std::string make_vcvars_text() {
    return "INCLUDE=C:\\VS\\include\r\nPath=C:\\VS\\bin;C:\\Windows\r\nSystemRoot=C:\\Windows\r\n"
           "VCToolsInstallDir=C:\\VS\\VC\\Tools\\MSVC\\14.44\\\r\nVSCMD_ARG_HOST_ARCH=x64\r\n"
           "VSCMD_ARG_TGT_ARCH=x64\r\n";
}

/** @brief Canned vcvars output: the listing as UTF-16LE. */
std::string make_vcvars_output() {
    return encode_utf16le(make_vcvars_text());
}

/** @brief Returns "vswhere" for any path ending in vswhere.exe, else @p program. */
std::string name_program(const std::string& program) {
    const std::string suffix = "\\Installer\\vswhere.exe";
    const bool is_vswhere =
        program.size() > suffix.size() && program.compare(program.size() - suffix.size(), suffix.size(), suffix) == 0;
    return is_vswhere ? "vswhere" : program;
}

/** @brief Fake runner for MSVC: cmd.exe answers vswhere and vcvarsall; programs in @p missing fail to start. */
struct FakeMsvcRunner {
    std::set<std::string> missing;
    int vswhere_exit_code = 0;
    std::string vswhere_output = "\r\nC:\\VS\r\n";
    std::string vcvars_output = make_vcvars_output();
    std::vector<std::string> programs;
    std::vector<std::string> vcvars_arguments;
    std::vector<std::string> last_environment;
    int last_is_environment_inherited = -1;

    [[nodiscard]] PrkProcessRunner make_runner() { return PrkProcessRunner{&FakeMsvcRunner::spawn, this}; }

private:
    static char* copy_text(const std::string& text) {
        char* const bytes = static_cast<char*>(std::malloc(text.size() + 1));
        if (bytes != nullptr) {
            std::memcpy(bytes, text.c_str(), text.size() + 1);
        }
        return bytes;
    }

    static PrkStatus finish(PrkProcessResult* out_result, int exit_code, const std::string& output) {
        std::memset(out_result, 0, sizeof *out_result);
        out_result->outcome = PRK_PROCESS_EXITED;
        out_result->exit_code = exit_code;
        out_result->standard_output = copy_text(output);
        out_result->standard_output_length = output.size();
        out_result->standard_error = copy_text("");
        return out_result->standard_output != nullptr && out_result->standard_error != nullptr
                   ? PRK_STATUS_OK
                   : PRK_STATUS_OUT_OF_MEMORY;
    }

    static PrkStatus spawn(void* state, const PrkProcessRequest* request, PrkProcessResult* out_result) {
        FakeMsvcRunner& fake = *static_cast<FakeMsvcRunner*>(state);
        const std::string program = name_program(request->executable);
        fake.programs.push_back(program);
        if (fake.missing.count(program) != 0) {
            return prk_set_process_start_failure(("executable not found: " + program).c_str(), out_result);
        }
        const std::vector<std::string> arguments(request->arguments, request->arguments + request->argument_count);
        if (program == "vswhere") {
            return finish(out_result, fake.vswhere_exit_code, fake.vswhere_output);
        }
        if (program == "cmd.exe") {
            fake.vcvars_arguments = arguments;
            return finish(out_result, 0, fake.vcvars_output);
        }
        fake.last_environment.assign(request->environment, request->environment + request->environment_count);
        fake.last_is_environment_inherited = request->is_environment_inherited;
        return finish(out_result, 0, "");
    }
};

std::vector<std::string> build_arguments(const PrkCompileRequest& request, PrkCompilerFamily family,
                                         const char* output) {
    PrkArgumentList list{};
    std::vector<std::string> arguments;
    if (prk_build_compiler_arguments(&request, family, output, &list) == PRK_STATUS_OK) {
        arguments.assign(list.items, list.items + list.count);
        prk_free_argument_list(&list);
    }
    return arguments;
}

void test_builds_object_and_library_arguments(PrkTestContext* context, void*) {
    const char* const sources[] = {"util.c"};
    PrkCompileRequest request{};
    request.sources = sources;
    request.source_count = 1;
    request.kind = PRK_OUTPUT_OBJECT;
    PRK_EXPECT(
        build_arguments(request, PRK_COMPILER_GCC, "util.o") ==
        std::vector<std::string>({"-fdiagnostics-color=never", "-Wall", "-Wextra", "-c", "util.c", "-o", "util.o"}));
    PRK_EXPECT(build_arguments(request, PRK_COMPILER_MSVC, "util.obj") ==
               std::vector<std::string>({"/nologo", "/W4", "/c", "util.c", "/Foutil.obj"}));
    const char* const objects[] = {"a.o", "b.o"};
    request.sources = objects;
    request.source_count = 2;
    request.kind = PRK_OUTPUT_STATIC_LIBRARY;
    PRK_EXPECT(build_arguments(request, PRK_COMPILER_CLANG, "libab.a") ==
               std::vector<std::string>({"rcs", "libab.a", "a.o", "b.o"}));
    PRK_EXPECT(build_arguments(request, PRK_COMPILER_MSVC, "ab.lib") ==
               std::vector<std::string>({"/nologo", "/OUT:ab.lib", "a.o", "b.o"}));
}

void test_builds_link_arguments(PrkTestContext* context, void*) {
    const char* const sources[] = {"main.c"};
    const char* const libraries[] = {"m", "libextra.a"};
    const char* const directories[] = {"lib"};
    PrkCompileRequest request{};
    request.sources = sources;
    request.source_count = 1;
    request.libraries = libraries;
    request.library_count = 2;
    request.library_directories = directories;
    request.library_directory_count = 1;
    PRK_EXPECT(build_arguments(request, PRK_COMPILER_GCC, "app") ==
               std::vector<std::string>({"-fdiagnostics-color=never", "-Wall", "-Wextra", "main.c", "-o", "app",
                                         "-Llib", "-lm", "libextra.a"}));
    PRK_EXPECT(build_arguments(request, PRK_COMPILER_MSVC, "app.exe") ==
               std::vector<std::string>(
                   {"/nologo", "/W4", "main.c", "/Feapp.exe", "/link", "/LIBPATH:lib", "m.lib", "libextra.a"}));
    request.kind = PRK_OUTPUT_SHARED_LIBRARY;
    request.library_count = 0;
    request.library_directory_count = 0;
    PRK_EXPECT(build_arguments(request, PRK_COMPILER_MSVC, "plugin.dll") ==
               std::vector<std::string>({"/nologo", "/W4", "/LD", "main.c", "/Feplugin.dll"}));
#if defined(_WIN32)
    const std::vector<std::string> shared_flags = {"-shared"};
#else
    const std::vector<std::string> shared_flags = {"-shared", "-fPIC"};
#endif
    std::vector<std::string> expected = {"-fdiagnostics-color=never", "-Wall", "-Wextra"};
    expected.insert(expected.end(), shared_flags.begin(), shared_flags.end());
    expected.insert(expected.end(), {"main.c", "-o", "libplugin.so"});
    PRK_EXPECT(build_arguments(request, PRK_COMPILER_GCC, "libplugin.so") == expected);
}

void test_names_output_kinds_and_paths(PrkTestContext* context, void*) {
    PRK_EXPECT(std::strcmp(prk_get_output_kind_name(PRK_OUTPUT_EXECUTABLE), "executable") == 0);
    PRK_EXPECT(std::strcmp(prk_get_output_kind_name(PRK_OUTPUT_OBJECT), "object") == 0);
    PRK_EXPECT(std::strcmp(prk_get_output_kind_name(PRK_OUTPUT_STATIC_LIBRARY), "static_library") == 0);
    PRK_EXPECT(std::strcmp(prk_get_output_kind_name(PRK_OUTPUT_SHARED_LIBRARY), "shared_library") == 0);
    std::array<char, 32> path{};
    prk_format_compile_output_path("build/util", PRK_OUTPUT_OBJECT, path.data(), path.size());
    PRK_EXPECT(std::strcmp(path.data(), "build/util") == 0); /* Only executables get ".exe". */
    prk_format_compile_output_path("a-very-long-output-path-that-is-cut", PRK_OUTPUT_EXECUTABLE, path.data(),
                                   path.size());
    PRK_EXPECT(std::strlen(path.data()) == path.size() - 1);
}

void test_parses_msvc_discovery_output(PrkTestContext* context, void*) {
    std::array<char, 64> path{};
    const std::string vswhere = "\r\n  C:\\Program Files\\Microsoft Visual Studio\\2022\\Community \r\nC:\\Other\r\n";
    PRK_EXPECT(prk_parse_vswhere_output(vswhere.c_str(), vswhere.size(), path.data(), path.size()) == 1);
    PRK_EXPECT(std::strcmp(path.data(), "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community") == 0);
    PRK_EXPECT(prk_parse_vswhere_output("C:\\%TEMP%\\VS", 12, path.data(), path.size()) == 0);
    PRK_EXPECT(prk_parse_vswhere_output("C:\\\"VS", 6, path.data(), path.size()) == 0);
    PRK_EXPECT(prk_parse_vswhere_output("\r\n\r\n", 4, path.data(), path.size()) == 0);
    PRK_EXPECT(prk_parse_vswhere_output(nullptr, 0, path.data(), path.size()) == 0);
    PrkMsvcToolchain toolchain{};
    const std::string vcvars = make_vcvars_output();
    PRK_EXPECT(prk_parse_vcvars_output(vcvars.c_str(), vcvars.size(), &toolchain) == PRK_STATUS_OK);
    PRK_EXPECT(toolchain.is_found == 1 && std::strcmp(toolchain.cl_path, fake_cl_path) == 0);
    PRK_EXPECT(std::strcmp(toolchain.lib_path, "C:\\VS\\VC\\Tools\\MSVC\\14.44\\bin\\Hostx64\\x64\\lib.exe") == 0);
    PRK_EXPECT(toolchain.environment_count == 6 &&
               std::strcmp(toolchain.environment[1], "Path=C:\\VS\\bin;C:\\Windows") == 0);
    prk_release_msvc_toolchain(&toolchain);
    const std::string narrow = "vcvarsall noise\r\n" + make_vcvars_text(); /* Without /u: 8-bit text. */
    PRK_EXPECT(prk_parse_vcvars_output(narrow.c_str(), narrow.size(), &toolchain) == PRK_STATUS_OK);
    PRK_EXPECT(toolchain.is_found == 1 && toolchain.environment_count == 6);
    prk_release_msvc_toolchain(&toolchain);
    const std::string no_cl = encode_utf16le("Path=C:\\x\r\nVSCMD_ARG_TGT_ARCH=x86\r\n");
    PRK_EXPECT(prk_parse_vcvars_output(no_cl.c_str(), no_cl.size(), &toolchain) == PRK_STATUS_OK);
    PRK_EXPECT(toolchain.is_found == 0 && std::strstr(toolchain.error_message, "VCToolsInstallDir") != nullptr);
    prk_release_msvc_toolchain(&toolchain);
    PRK_EXPECT(prk_is_msvc_architecture_allowed("x86") == 1 && prk_is_msvc_architecture_allowed("x64_arm64") == 1);
    PRK_EXPECT(prk_is_msvc_architecture_allowed("x64 x86") == 0 && prk_is_msvc_architecture_allowed(nullptr) == 0);
    const std::string native = prk_get_native_msvc_architecture();
    PRK_EXPECT(native == "x64" || native == "x86" || native == "arm64");
}

void test_discovers_msvc_through_runner(PrkTestContext* context, void*) {
    FakeMsvcRunner fake;
    const PrkProcessRunner runner = fake.make_runner();
    PrkMsvcToolchain toolchain{};
    PRK_EXPECT(prk_discover_msvc_toolchain(&runner, "x64", &toolchain) == PRK_STATUS_OK);
    PRK_EXPECT(toolchain.is_attempted == 1 && toolchain.is_found == 1);
    PRK_EXPECT(std::strcmp(toolchain.installation_path, "C:\\VS") == 0 &&
               std::strcmp(toolchain.architecture, "x64") == 0);
    PRK_EXPECT(fake.programs == std::vector<std::string>({"vswhere", "cmd.exe"}));
    PRK_EXPECT(fake.vcvars_arguments.size() == 9 &&
               fake.vcvars_arguments[4] == "C:\\VS\\VC\\Auxiliary\\Build\\vcvarsall.bat");
    fake.vswhere_exit_code = 1;
    PRK_EXPECT(prk_discover_msvc_toolchain(&runner, nullptr, &toolchain) == PRK_STATUS_OK);
    PRK_EXPECT(toolchain.is_attempted == 1 && toolchain.is_found == 0 && toolchain.environment == nullptr);
    PRK_EXPECT(std::strstr(toolchain.error_message, "vswhere") != nullptr);
    fake.vswhere_exit_code = 0;
    fake.vswhere_output = "";
    PRK_EXPECT(prk_discover_msvc_toolchain(&runner, nullptr, &toolchain) == PRK_STATUS_OK);
    PRK_EXPECT(toolchain.is_found == 0 && std::strstr(toolchain.error_message, "C++ build tools") != nullptr);
    PRK_EXPECT(prk_discover_msvc_toolchain(&runner, "x64 extra", &toolchain) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_discover_msvc_toolchain(nullptr, nullptr, &toolchain) == PRK_STATUS_INVALID_ARGUMENT);
    prk_release_msvc_toolchain(&toolchain);
    prk_release_msvc_toolchain(nullptr);
}

void test_msvc_falls_back_to_discovery(PrkTestContext* context, void*) {
    FakeMsvcRunner fake;
    fake.missing = {"cl", "lib"};
    const PrkProcessRunner runner = fake.make_runner();
    const char* const sources[] = {"hello.c"};
    PrkMsvcToolchain cache{};
    PrkCompileRequest request{};
    request.family = PRK_COMPILER_MSVC;
    request.sources = sources;
    request.source_count = 1;
    request.output_path = "hello.exe";
    request.msvc_toolchain = &cache;
    PrkCompileResult result{};
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(fake.programs == std::vector<std::string>({"cl", "vswhere", "cmd.exe", fake_cl_path}));
    PRK_EXPECT(result.process.outcome == PRK_PROCESS_EXITED && result.is_msvc_discovered == 1);
    PRK_EXPECT(fake.last_is_environment_inherited == 0 && fake.last_environment.size() == 6);
    PRK_EXPECT(std::strcmp(result.compiler_program, fake_cl_path) == 0);
    prk_release_compile_result(&result);
    fake.programs.clear();
    request.kind = PRK_OUTPUT_STATIC_LIBRARY;
    request.output_path = "hello.lib";
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(fake.programs ==
               std::vector<std::string>({"C:\\VS\\VC\\Tools\\MSVC\\14.44\\bin\\Hostx64\\x64\\lib.exe"}));
    prk_release_compile_result(&result);
    prk_release_msvc_toolchain(&cache);
}

void test_missing_msvc_is_a_result(PrkTestContext* context, void*) {
    FakeMsvcRunner fake;
    fake.missing = {"cl", "clang", "gcc"};
    fake.vswhere_exit_code = 1;
    const PrkProcessRunner runner = fake.make_runner();
    const char* const sources[] = {"hello.c"};
    PrkCompileRequest request{};
    request.family = PRK_COMPILER_MSVC;
    request.sources = sources;
    request.source_count = 1;
    request.output_path = "hello.exe";
    PrkCompileResult result{};
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(result.process.outcome == PRK_PROCESS_FAILED_TO_START && result.is_msvc_discovered == 0);
    PRK_EXPECT(std::strstr(result.process.error_message, "cl is not on PATH and MSVC discovery failed: vswhere") !=
               nullptr);
    prk_release_compile_result(&result);
    request.compiler_program = "cl-custom"; /* An explicit program is never replaced by discovery. */
    fake.missing.insert("cl-custom");
    fake.programs.clear();
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(fake.programs == std::vector<std::string>({"cl-custom"}));
    prk_release_compile_result(&result);
    request.compiler_program = nullptr;
    request.kind = PRK_OUTPUT_OBJECT;
    const char* const two[] = {"a.c", "b.c"};
    request.sources = two;
    request.source_count = 2;
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_INVALID_ARGUMENT);
}

void test_lua_compile_library_options(PrkTestContext* context, void*) {
    FakeMsvcRunner fake;
    LuaScriptRunSetup setup;
    setup.process_runner = fake.make_runner();
    const auto run = load_and_run_lua_script(
        "prk.add_test('archive', function()\n"
        "  local r = prk.compile{sources = {'a.o', 'b.o'}, output = 'libab.a', kind = 'static_library',\n"
        "    compiler = 'gcc', archiver = 'x86_64-w64-mingw32-ar'}\n"
        "  prk.expect_exit_code(r, 0) prk.expect_equal(r.kind, 'static_library')\n"
        "  prk.expect_equal(r.program, 'x86_64-w64-mingw32-ar') prk.expect_false(r.msvc_discovered)\n"
        "  prk.expect_equal(r.command, 'x86_64-w64-mingw32-ar rcs libab.a a.o b.o')\n"
        "end)\n"
        "prk.add_test('link', function()\n"
        "  local r = prk.compile{sources = 'main.c', output = 'app.out', compiler = 'clang', libraries = {'m'},\n"
        "    library_dirs = 'lib'}\n"
        "  prk.expect_match(r.command, ' %-Llib %-lm$')\n"
        "end)\n"
        "prk.add_test('object_needs_one', function()\n"
        "  prk.compile{sources = {'a.c', 'b.c'}, output = 'a.o', kind = 'object'} end)\n"
        "prk.add_test('bad_arch', function()\n"
        "  prk.compile{sources = 'a.c', output = 'a', msvc_arch = 'x64 extra'} end)\n"
        "prk.add_test('bad_kind', function() prk.compile{sources = 'a.c', output = 'a', kind = 'dll'} end)\n",
        std::move(setup));
    PRK_EXPECT(run->results.size() == 5 && run->results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(run->results.size() == 5 && run->results[1].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(run->results.size() == 5 && contains_text(run->results[2].message, "exactly one source"));
    PRK_EXPECT(run->results.size() == 5 && contains_text(run->results[3].message, "msvc_arch must be"));
    PRK_EXPECT(run->results.size() == 5 && contains_text(run->results[4].message, "unknown kind 'dll'"));
}

void test_discovers_real_msvc(PrkTestContext* context, void*) {
#if defined(_WIN32)
    const PrkProcessRunner runner = prk_get_system_process_runner();
    PrkMsvcToolchain toolchain{};
    PRK_EXPECT(prk_discover_msvc_toolchain(&runner, nullptr, &toolchain) == PRK_STATUS_OK);
    if (!toolchain.is_found) {
        const std::string reason = std::string("no Visual Studio C++ tools: ") + toolchain.error_message;
        prk_release_msvc_toolchain(&toolchain);
        prk_skip_test(context, reason.c_str());
        return;
    }
    PRK_EXPECT(std::strstr(toolchain.cl_path, "cl.exe") != nullptr && toolchain.environment_count > 0);
    const char* const sources[] = {"prk_msvc_discovery_selftest.c"};
    PrkCompileRequest request{};
    request.family = PRK_COMPILER_MSVC;
    request.sources = sources;
    request.source_count = 1;
    request.output_path = "prk_msvc_discovery_selftest";
    request.msvc_toolchain = &toolchain;
    {
        std::ofstream file(sources[0], std::ios::binary);
        file << "int main(void) { return 7; }\n";
        PRK_EXPECT(static_cast<bool>(file));
    }
    PrkCompileResult result{};
    PRK_EXPECT(prk_compile_program(&runner, &request, &result) == PRK_STATUS_OK);
    PRK_EXPECT(result.is_msvc_discovered == 1 && result.process.outcome == PRK_PROCESS_EXITED &&
               result.process.exit_code == 0);
    (void)std::remove(result.output_path);
    (void)std::remove("prk_msvc_discovery_selftest.obj");
    (void)std::remove(sources[0]);
    prk_release_compile_result(&result);
    prk_release_msvc_toolchain(&toolchain);
#else
    prk_skip_test(context, "MSVC discovery runs on Windows only");
#endif
}

}  // namespace

PrkStatus register_c_driver_toolchain_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"c_driver.builds_object_and_library_arguments", &test_builds_object_and_library_arguments, nullptr},
        {"c_driver.builds_link_arguments", &test_builds_link_arguments, nullptr},
        {"c_driver.names_output_kinds_and_paths", &test_names_output_kinds_and_paths, nullptr},
        {"c_driver.parses_msvc_discovery_output", &test_parses_msvc_discovery_output, nullptr},
        {"c_driver.discovers_msvc_through_runner", &test_discovers_msvc_through_runner, nullptr},
        {"c_driver.msvc_falls_back_to_discovery", &test_msvc_falls_back_to_discovery, nullptr},
        {"c_driver.missing_msvc_is_a_result", &test_missing_msvc_is_a_result, nullptr},
        {"c_driver.lua_compile_library_options", &test_lua_compile_library_options, nullptr},
        {"c_driver.discovers_real_msvc", &test_discovers_real_msvc, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
