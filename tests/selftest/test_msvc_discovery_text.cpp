/**
 * @file test_msvc_discovery_text.cpp
 * @brief Tests for the text side of MSVC discovery (#14, #74): the vswhere path, the vcvarsall command line, and
 *        the UTF-16LE decoding of `cmd.exe /u` output.
 */
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "drivers/prk_c_driver_internal.h"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

void test_builds_vcvars_arguments(PrkTestContext* context, void*) {
    PrkArgumentList list{};
    PRK_EXPECT(prk_build_vcvars_arguments("C:\\VS 2022", "x86", &list) == PRK_STATUS_OK);
    const std::vector<std::string> arguments(list.items, list.items + list.count);
    prk_free_argument_list(&list);
    PRK_EXPECT(arguments ==
               std::vector<std::string>({"/d", "/u", "/c", "call", "C:\\VS 2022\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                                         "x86", ">nul", "&&", "set"}));
    std::array<char, 96> vswhere{};
    prk_build_vswhere_path("D:\\Apps", vswhere.data(), vswhere.size());
    PRK_EXPECT(std::strcmp(vswhere.data(), "D:\\Apps\\Microsoft Visual Studio\\Installer\\vswhere.exe") == 0);
    prk_build_vswhere_path(nullptr, vswhere.data(), vswhere.size());
    PRK_EXPECT(
        std::strcmp(vswhere.data(), "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe") == 0);
}

/** @brief Decodes @p wide; "<narrow>" when the input is not UTF-16LE. */
std::string decode(const std::string& wide) {
    char* text = nullptr;
    size_t length = 0;
    if (prk_decode_utf16le_if_wide(wide.data(), wide.size(), &text, &length) != PRK_STATUS_OK) {
        return "<error>";
    }
    const std::string decoded = text != nullptr ? std::string(text, length) : "<narrow>";
    std::free(text);
    return decoded;
}

void test_decodes_utf16le_output(PrkTestContext* context, void*) {
    PRK_EXPECT(decode(std::string("A\0=\0\xE9\0\r\0\n\0", 10)) == "A=\xC3\xA9\r\n");
    PRK_EXPECT(decode(std::string("X\0\x3D\xD8\x00\xDE", 6)) == "X\xF0\x9F\x98\x80"); /* U+1F600. */
    PRK_EXPECT(decode(std::string("X\0\x00\xD8", 4)) == "X\xEF\xBF\xBD");             /* Lone surrogate. */
    PRK_EXPECT(decode(std::string("X\0\xAC\x20", 4)) == "X\xE2\x82\xAC");             /* U+20AC. */
    PRK_EXPECT(decode("Path=C:\\x") == "<narrow>");
    PRK_EXPECT(decode("") == "<narrow>");
}

}  // namespace

PrkStatus register_msvc_discovery_text_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"c_driver.builds_vcvars_arguments", &test_builds_vcvars_arguments, nullptr},
        {"c_driver.decodes_utf16le_output", &test_decodes_utf16le_output, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
