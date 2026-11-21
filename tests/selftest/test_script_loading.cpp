/**
 * @file test_script_loading.cpp
 * @brief Internal tests for the script host (#9): the data-path policy and the C host adapter.
 *        Cases that prk scripts cover are in prk.tests.json (docs/self-hosting.md, #96).
 */
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cpp/command_line_app.hpp"
#include "cpp/script_host.hpp"
#include "cpp/script_loading.hpp"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

void test_script_data_path_policy(PrkTestContext* context, void*) {
    for (const std::string_view allowed : {"a.txt", "out/report.xml", "out\\report.xml", "./a", "a..b", "..a/b"}) {
        PRK_EXPECT(is_script_data_path_allowed(allowed));
    }
    for (const std::string_view denied : {"", "/etc/passwd", "\\\\server\\share", "C:\\x", "C:x", "..", "../a",
                                          "a/../../b", "a\\..\\b", "file.txt:stream"}) {
        PRK_EXPECT(!is_script_data_path_allowed(denied));
    }
    PRK_EXPECT(!is_script_data_path_allowed(std::string_view("a\0b", 3)));
}

void receive_into_string(void* sink, const char* bytes, std::size_t length) {
    static_cast<std::string*>(sink)->append(bytes, length);
}

void test_script_host_applies_policy_and_contains_exceptions(PrkTestContext* context, void*) {
    std::ostringstream printed;
    ScriptHostServices services;
    services.read_monotonic_milliseconds = [] { return std::uint64_t{42}; };
    services.find_file = [](const std::string& path) {
        if (path == "throws") {
            throw std::runtime_error("disk on fire");
        }
        return path == "a.txt" ? PRK_FILE_ACCESS_OK : PRK_FILE_ACCESS_NOT_FOUND;
    };
    services.read_file = [](const std::string&, std::string& out_bytes) {
        out_bytes = "bytes";
        return PRK_FILE_ACCESS_OK;
    };
    ScriptHost host(services, printed);
    const PrkScriptHost c_host = host.make_c_host();
    c_host.write_text(c_host.host_state, "hi", 2);
    PRK_EXPECT(printed.str() == "hi");
    PRK_EXPECT(c_host.read_monotonic_milliseconds(c_host.host_state) == 42);
    PRK_EXPECT(c_host.find_file(c_host.host_state, "a.txt") == PRK_FILE_ACCESS_OK);
    PRK_EXPECT(c_host.find_file(c_host.host_state, "b.txt") == PRK_FILE_ACCESS_NOT_FOUND);
    PRK_EXPECT(c_host.find_file(c_host.host_state, "../a.txt") == PRK_FILE_ACCESS_DENIED);
    PRK_EXPECT(c_host.find_file(c_host.host_state, "throws") == PRK_FILE_ACCESS_READ_ERROR);
    std::string bytes;
    PRK_EXPECT(c_host.read_file(c_host.host_state, "a.txt", &receive_into_string, &bytes) == PRK_FILE_ACCESS_OK);
    PRK_EXPECT(bytes == "bytes");
    PRK_EXPECT(c_host.read_file(c_host.host_state, "/abs", &receive_into_string, &bytes) == PRK_FILE_ACCESS_DENIED);
    ScriptHost bare(ScriptHostServices{}, printed);
    const PrkScriptHost bare_host = bare.make_c_host();
    PRK_EXPECT(bare_host.write_text != nullptr && bare_host.read_monotonic_milliseconds == nullptr);
    PRK_EXPECT(bare_host.find_file == nullptr && bare_host.read_file == nullptr);
}

}  // namespace

PrkStatus register_script_loading_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"scripts.host.data_path_policy", &test_script_data_path_policy, nullptr},
        {"scripts.host.applies_policy_and_contains_exceptions",
         &test_script_host_applies_policy_and_contains_exceptions, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
