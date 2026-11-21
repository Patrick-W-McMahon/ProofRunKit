/**
 * @file test_process_spawn.cpp
 * @brief Tests for the process-spawn layer (#13): request validation, the environment merge, Windows
 *        argument quoting, bounded output, and real spawns of prk_spawn_child (success, non-zero exit,
 *        environment, working directory, stdin, timeout, missing executable, output limit).
 */
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
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

/** @brief Directory of the running self-test executable, with a trailing separator ("" if unknown). */
std::string find_selftest_directory() {
#if defined(_WIN32)
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    const int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(size > 0 ? size : 1), '\0');
    (void)WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, text.data(), size, nullptr, nullptr);
    text.resize(text.find('\0'));
#else
    std::string text(4096, '\0');
    const ssize_t length = readlink("/proc/self/exe", text.data(), text.size() - 1);
    text.resize(length > 0 ? static_cast<std::size_t>(length) : 0);
#endif
    const std::size_t separator = text.find_last_of("/\\");
    return separator == std::string::npos ? std::string() : text.substr(0, separator + 1);
}

std::string find_child_path() {
#if defined(_WIN32)
    return find_selftest_directory() + "prk_spawn_child.exe";
#else
    return find_selftest_directory() + "prk_spawn_child";
#endif
}

/** @brief Owns a result and releases it. */
struct SpawnedChild {
    PrkStatus status = PRK_STATUS_OK;
    PrkProcessResult result{};
    SpawnedChild() = default;
    SpawnedChild(const SpawnedChild&) = delete;
    SpawnedChild& operator=(const SpawnedChild&) = delete;
    ~SpawnedChild() { prk_release_process_result(&result); }
    [[nodiscard]] std::string output() const {
        return result.standard_output != nullptr ? std::string(result.standard_output, result.standard_output_length)
                                                 : std::string();
    }
    [[nodiscard]] std::string error() const {
        return result.standard_error != nullptr ? std::string(result.standard_error, result.standard_error_length)
                                                : std::string();
    }
};

/** @brief Options for spawn_child beyond the arguments. */
struct ChildSetup {
    std::vector<std::string> environment;
    bool is_environment_inherited = true;
    std::string working_directory;
    std::string standard_input;
    std::uint64_t timeout_ms = 30000;
    std::size_t output_limit_bytes = 0;
    std::string executable;  ///< Empty: prk_spawn_child.
};

std::unique_ptr<SpawnedChild> spawn_child(const std::vector<std::string>& arguments, const ChildSetup& setup = {}) {
    const std::string executable = setup.executable.empty() ? find_child_path() : setup.executable;
    std::vector<const char*> argument_pointers;
    for (const std::string& argument : arguments) {
        argument_pointers.push_back(argument.c_str());
    }
    std::vector<const char*> environment_pointers;
    for (const std::string& entry : setup.environment) {
        environment_pointers.push_back(entry.c_str());
    }
    const PrkProcessRequest request{executable.c_str(),
                                    argument_pointers.data(),
                                    argument_pointers.size(),
                                    setup.working_directory.c_str(),
                                    environment_pointers.data(),
                                    environment_pointers.size(),
                                    setup.is_environment_inherited ? 1 : 0,
                                    setup.standard_input.data(),
                                    setup.standard_input.size(),
                                    setup.timeout_ms,
                                    setup.output_limit_bytes};
    auto child = std::make_unique<SpawnedChild>();
    const PrkProcessRunner runner = prk_get_system_process_runner();
    child->status = runner.spawn_process(runner.runner_state, &request, &child->result);
    return child;
}

/** @brief Removes trailing separators and, on Windows, compares without case. */
std::string normalize_directory(std::string text) {
    while (text.size() > 1 &&
           (text.back() == '/' || text.back() == '\\' || text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
#if defined(_WIN32)
    for (char& character : text) {
        character = character == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
#endif
    return text;
}

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

void test_spawns_and_captures_output(PrkTestContext* context, void*) {
    const auto echoed = spawn_child({"echo", "plain", "two words", "", "quote\"inside", "trailing\\", "a\\\"b"});
    PRK_EXPECT(echoed->status == PRK_STATUS_OK && echoed->result.outcome == PRK_PROCESS_EXITED);
    PRK_EXPECT(echoed->result.exit_code == 0);
    PRK_EXPECT(echoed->output() == "[plain]\n[two words]\n[]\n[quote\"inside]\n[trailing\\]\n[a\\\"b]\n");
    const auto failed = spawn_child({"exit", "7"});
    PRK_EXPECT(failed->result.outcome == PRK_PROCESS_EXITED && failed->result.exit_code == 7);
    PRK_EXPECT(failed->output() == "out\n" && failed->error() == "err\n");
    PRK_EXPECT(failed->result.elapsed_ms < 30000);
}

void test_sets_environment_and_directory(PrkTestContext* context, void*) {
    ChildSetup setup;
    setup.environment = {"PRK_SPAWN_TEST=hello world"};
    const auto inherited = spawn_child({"env", "PRK_SPAWN_TEST"}, setup);
    PRK_EXPECT(inherited->output() == "hello world\n");
    setup.is_environment_inherited = false;
    const auto isolated = spawn_child({"env", "PATH"}, setup);
    PRK_EXPECT(isolated->result.outcome == PRK_PROCESS_EXITED && isolated->output() == "(unset)\n");
    ChildSetup directory;
    directory.working_directory = find_selftest_directory();
    const auto moved = spawn_child({"cwd"}, directory);
    PRK_EXPECT(normalize_directory(moved->output()) == normalize_directory(directory.working_directory));
}

void test_feeds_standard_input(PrkTestContext* context, void*) {
    ChildSetup setup;
    setup.standard_input = std::string("line one\nline two\n") + std::string(200000, 'z');
    const auto copied = spawn_child({"cat"}, setup);
    PRK_EXPECT(copied->result.outcome == PRK_PROCESS_EXITED && copied->output() == setup.standard_input);
    const auto ignored = spawn_child({"exit", "0"}, setup);  // Exits without reading stdin: no hang, no SIGPIPE.
    PRK_EXPECT(ignored->result.outcome == PRK_PROCESS_EXITED && ignored->output() == "out\n");
}

void test_kills_process_on_timeout(PrkTestContext* context, void*) {
    ChildSetup setup;
    setup.timeout_ms = 300;
    const auto slow = spawn_child({"sleep", "20000"}, setup);
    PRK_EXPECT(slow->status == PRK_STATUS_OK && slow->result.outcome == PRK_PROCESS_TIMED_OUT);
    PRK_EXPECT(slow->result.elapsed_ms >= 250 && slow->result.elapsed_ms < 15000);
}

void test_reports_missing_executable(PrkTestContext* context, void*) {
    ChildSetup setup;
    setup.executable = "prk_no_such_program_4711";
    const auto missing = spawn_child({}, setup);
    PRK_EXPECT(missing->status == PRK_STATUS_OK && missing->result.outcome == PRK_PROCESS_FAILED_TO_START);
    PRK_EXPECT(std::string(missing->result.error_message) == "executable not found: prk_no_such_program_4711");
    PRK_EXPECT(missing->output().empty() && missing->error().empty());
    ChildSetup bad_directory;
    bad_directory.working_directory = find_selftest_directory() + "no_such_directory_4711";
    const auto lost = spawn_child({"cwd"}, bad_directory);
    PRK_EXPECT(lost->result.outcome == PRK_PROCESS_FAILED_TO_START);
    PRK_EXPECT(std::string(lost->result.error_message).find("working directory") != std::string::npos);
}

void test_limits_captured_output(PrkTestContext* context, void*) {
    ChildSetup setup;
    setup.output_limit_bytes = 1000;
    const auto flood = spawn_child({"flood", "300000"}, setup);
    PRK_EXPECT(flood->result.outcome == PRK_PROCESS_EXITED && flood->result.exit_code == 0);
    PRK_EXPECT(flood->result.standard_output_length == 1000 && flood->result.is_standard_output_truncated == 1);
    PRK_EXPECT(flood->result.standard_error_length == 1000 && flood->result.is_standard_error_truncated == 1);
}

void test_reads_environment_variable(PrkTestContext* context, void*) {
    std::array<char, 4096> value{};
    PRK_EXPECT(prk_read_environment_variable("PATH", value.data(), value.size()) == 1 && value[0] != '\0');
    std::array<char, 2> tiny{{'x', 'x'}};
    PRK_EXPECT(prk_read_environment_variable("PATH", tiny.data(), tiny.size()) == 0 && tiny[0] == '\0');
    PRK_EXPECT(prk_read_environment_variable("PRK_NO_SUCH_VARIABLE_4711", value.data(), value.size()) == 0);
    PRK_EXPECT(value[0] == '\0');
    PRK_EXPECT(prk_read_environment_variable("", value.data(), value.size()) == 0);
    PRK_EXPECT(prk_read_environment_variable("PATH", nullptr, 0) == 0);
}

}  // namespace

PrkStatus register_process_spawn_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"process.validates_requests", &test_validates_requests, nullptr},
        {"process.merges_environment", &test_merges_environment, nullptr},
        {"process.quotes_windows_arguments", &test_quotes_windows_arguments, nullptr},
        {"process.bounds_captured_output", &test_bounds_captured_output, nullptr},
        {"process.spawns_and_captures_output", &test_spawns_and_captures_output, nullptr},
        {"process.sets_environment_and_directory", &test_sets_environment_and_directory, nullptr},
        {"process.feeds_standard_input", &test_feeds_standard_input, nullptr},
        {"process.kills_process_on_timeout", &test_kills_process_on_timeout, nullptr},
        {"process.reports_missing_executable", &test_reports_missing_executable, nullptr},
        {"process.limits_captured_output", &test_limits_captured_output, nullptr},
        {"process.reads_environment_variable", &test_reads_environment_variable, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
