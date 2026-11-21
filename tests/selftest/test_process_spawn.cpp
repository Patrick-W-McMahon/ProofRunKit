/**
 * @file test_process_spawn.cpp
 * @brief Internal tests for the process-spawn layer (#13): request validation, the environment merge, Windows
 *        argument quoting, bounded output, and environment-variable reads.
 *        Cases that prk scripts cover are in prk.tests.json (docs/self-hosting.md, #96).
 */
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "process/prk_process_internal.h"
#include "proofrunkit/prk_process.h"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

std::string format_command_line(std::string_view executable, const std::vector<const char*>& arguments) {
    char* text = nullptr;
    const std::string program(executable);
    if (prk_format_windows_command_line(program.c_str(), arguments.data(), arguments.size(), &text) != PRK_STATUS_OK) {
        return "(error)";
    }
    std::string result(text);
    free(text);
    return result;
}

void test_validates_requests(PrkTestContext* context, void*) {
    PRK_EXPECT(prk_check_process_request(nullptr) == PRK_STATUS_INVALID_ARGUMENT);
    PrkProcessRequest request{};
    PRK_EXPECT(prk_check_process_request(&request) == PRK_STATUS_INVALID_ARGUMENT);
    request.executable = "";
    PRK_EXPECT(prk_check_process_request(&request) == PRK_STATUS_INVALID_ARGUMENT);
    request.executable = "tool";
    PRK_EXPECT(prk_check_process_request(&request) == PRK_STATUS_OK);
    PRK_EXPECT(prk_get_effective_process_timeout_ms(&request) == PRK_PROCESS_DEFAULT_TIMEOUT_MS);
    request.argument_count = 1;
    PRK_EXPECT(prk_check_process_request(&request) == PRK_STATUS_INVALID_ARGUMENT);
    const char* const bad_environment[] = {"=value"};
    request.argument_count = 0;
    request.environment = bad_environment;
    request.environment_count = 1;
    PRK_EXPECT(prk_check_process_request(&request) == PRK_STATUS_INVALID_ARGUMENT);
    request.environment_count = 0;
    request.timeout_ms = PRK_PROCESS_MAX_TIMEOUT_MS + 1;
    PRK_EXPECT(prk_check_process_request(&request) == PRK_STATUS_INVALID_ARGUMENT);
    request.timeout_ms = 250;
    PRK_EXPECT(prk_get_effective_process_timeout_ms(&request) == 250);
    PRK_EXPECT(std::string(prk_get_process_outcome_name(PRK_PROCESS_TIMED_OUT)) == "timed out");
    PrkProcessResult unused{};
    PRK_EXPECT(prk_spawn_system_process(nullptr, &request, nullptr) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_spawn_system_process(nullptr, nullptr, &unused) == PRK_STATUS_INVALID_ARGUMENT);
    prk_release_process_result(nullptr);
}

void test_merges_environment(PrkTestContext* context, void*) {
    const char* const parent[] = {"Path=C:\\bin", "HOME=/home/a", "=C:=C:\\work"};
    const char* const overrides[] = {"PATH=D:\\tools", "NEW=1", "NEW=2"};
    PrkProcessRequest request{};
    request.executable = "tool";
    request.environment = overrides;
    request.environment_count = 3;
    request.is_environment_inherited = 1;
    const char** merged = nullptr;
    std::size_t count = 0;
    PRK_EXPECT(prk_merge_process_environment(parent, 3, &request, 1, &merged, &count) == PRK_STATUS_OK);
    std::vector<std::string> entries(merged, merged + count);
    free(static_cast<void*>(merged));
    PRK_EXPECT(entries == std::vector<std::string>({"HOME=/home/a", "=C:=C:\\work", "PATH=D:\\tools", "NEW=2"}));
    PRK_EXPECT(prk_merge_process_environment(parent, 3, &request, 0, &merged, &count) == PRK_STATUS_OK);
    entries.assign(merged, merged + count);
    free(static_cast<void*>(merged));
    PRK_EXPECT(entries.size() == 5 && entries[0] == "Path=C:\\bin");  // Case-sensitive: Path and PATH differ.
    request.is_environment_inherited = 0;
    PRK_EXPECT(prk_merge_process_environment(parent, 3, &request, 1, &merged, &count) == PRK_STATUS_OK);
    entries.assign(merged, merged + count);
    free(static_cast<void*>(merged));
    PRK_EXPECT(entries == std::vector<std::string>({"PATH=D:\\tools", "NEW=2"}));
}

void test_quotes_windows_arguments(PrkTestContext* context, void*) {
    PRK_EXPECT(format_command_line("gcc", {"-c", "a.c"}) == "gcc -c a.c");
    PRK_EXPECT(format_command_line("C:\\Program Files\\x.exe", {}) == "\"C:\\Program Files\\x.exe\"");
    PRK_EXPECT(format_command_line("t", {"two words", ""}) == "t \"two words\" \"\"");
    PRK_EXPECT(format_command_line("t", {"say \"hi\""}) == "t \"say \\\"hi\\\"\"");
    PRK_EXPECT(format_command_line("t", {"C:\\dir\\", "C:\\a b\\"}) == "t C:\\dir\\ \"C:\\a b\\\\\"");
    PRK_EXPECT(format_command_line("t", {"a\\\\\"b"}) == "t \"a\\\\\\\\\\\"b\"");
    char* text = nullptr;
    PRK_EXPECT(prk_format_windows_command_line("bad\"name", nullptr, 0, &text) == PRK_STATUS_INVALID_ARGUMENT);
}

void test_bounds_captured_output(PrkTestContext* context, void*) {
    PrkOutputBuffer buffer{};
    buffer.limit = 5;
    prk_append_process_output(&buffer, "abc", 3);
    prk_append_process_output(&buffer, "defgh", 5);
    char* text = nullptr;
    std::size_t length = 0;
    int is_truncated = 0;
    PRK_EXPECT(prk_take_process_output(&buffer, &text, &length, &is_truncated) == PRK_STATUS_OK);
    PRK_EXPECT(std::string(text, length) == "abcde" && is_truncated == 1 && text[5] == '\0');
    free(text);
    PrkOutputBuffer empty{};
    empty.limit = 5;
    PRK_EXPECT(prk_take_process_output(&empty, &text, &length, &is_truncated) == PRK_STATUS_OK);
    PRK_EXPECT(length == 0 && text[0] == '\0' && is_truncated == 0);
    free(text);
}

/** @brief Sets (or with nullptr removes) this process's variable @p name. @return true on success. */
bool set_test_environment_variable(const char* name, const char* value) {
#if defined(_WIN32)
    return _putenv_s(name, value != nullptr ? value : "") == 0;
#else
    return value != nullptr ? setenv(name, value, 1) == 0 : unsetenv(name) == 0;
#endif
}

/** @brief Fixed-buffer and heap reads of a known short and a known long (8000-byte) variable (#78). */
void test_reads_environment_variable(PrkTestContext* context, void*) {
    static const char kName[] = "PRK_SELFTEST_ENV_VALUE_78";
    std::array<char, 4096> value{};
    PRK_EXPECT(set_test_environment_variable(kName, "short value"));
    PRK_EXPECT(prk_read_environment_variable(kName, value.data(), value.size()) == 1);
    PRK_EXPECT(std::string_view(value.data()) == "short value");
    std::array<char, 2> tiny{{'x', 'x'}};
    PRK_EXPECT(prk_read_environment_variable(kName, tiny.data(), tiny.size()) == 0 && tiny[0] == '\0');

    const std::string long_value(8000, 'p');
    PRK_EXPECT(set_test_environment_variable(kName, long_value.c_str()));
    PRK_EXPECT(prk_read_environment_variable(kName, value.data(), value.size()) == 0 && value[0] == '\0');
    char* copy = nullptr;
    PRK_EXPECT(prk_duplicate_environment_variable(kName, &copy) == 1 && copy != nullptr && copy == long_value);
    std::free(copy);

    PRK_EXPECT(set_test_environment_variable(kName, nullptr));
    copy = reinterpret_cast<char*>(&tiny);
    PRK_EXPECT(prk_duplicate_environment_variable(kName, &copy) == 0 && copy == nullptr);
    PRK_EXPECT(prk_read_environment_variable(kName, value.data(), value.size()) == 0);
    PRK_EXPECT(prk_read_environment_variable("", value.data(), value.size()) == 0);
    PRK_EXPECT(prk_read_environment_variable(kName, nullptr, 0) == 0);
    PRK_EXPECT(prk_duplicate_environment_variable("", &copy) == 0 && copy == nullptr);
    PRK_EXPECT(prk_duplicate_environment_variable(kName, nullptr) == 0);
}

}  // namespace

PrkStatus register_process_spawn_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"process.validates_requests", &test_validates_requests, nullptr},
        {"process.merges_environment", &test_merges_environment, nullptr},
        {"process.quotes_windows_arguments", &test_quotes_windows_arguments, nullptr},
        {"process.bounds_captured_output", &test_bounds_captured_output, nullptr},
        {"process.reads_environment_variable", &test_reads_environment_variable, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
