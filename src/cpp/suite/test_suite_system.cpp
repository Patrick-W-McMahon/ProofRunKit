/**
 * @file test_suite_system.cpp
 * @brief Real services for `prk test` (#82): the system process runner, disk files via std::filesystem, and the
 *        path of the running executable.
 */
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "test_command.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace prk {

namespace {

/** @brief UTF-8 text to a path (std::filesystem::u8path is deprecated in C++20). */
std::filesystem::path make_file_path(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

/** @brief A path to UTF-8 text. */
std::string format_file_path(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

/** @brief Reads a whole file, or nullopt. */
std::optional<std::string> read_whole_file(const std::string& path) {
    std::ifstream file(make_file_path(path), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/** @brief Writes a whole file, creating its folders; returns "" or why not. */
std::string write_whole_file(const std::string& path, const std::string& text) {
    const std::filesystem::path file_path = make_file_path(path);
    std::error_code error;
    if (file_path.has_parent_path()) {
        std::filesystem::create_directories(file_path.parent_path(), error);
    }
    std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return "cannot open file for writing";
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return file ? std::string() : std::string("cannot write file");
}

/** @brief Tells whether a file or folder exists. */
bool does_path_exist(const std::string& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::exists(make_file_path(path), error);
}

/** @brief Returns @p path made absolute and normalised (native separators). */
std::string make_absolute_normal_path(const std::string& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(make_file_path(path), error);
    std::filesystem::path normal = (error ? make_file_path(path) : absolute).lexically_normal();
    std::string text = format_file_path(normal);
    while (text.size() > 1 && (text.back() == '/' || text.back() == '\\') && text[text.size() - 2] != ':') {
        text.pop_back();
    }
    return text;
}

/** @brief Returns the absolute path of the running executable, or "prk" when it cannot be found. */
std::string find_running_executable() {
#if defined(_WIN32)
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return "prk";
    }
    path.resize(length);
    return format_file_path(std::filesystem::path(path));
#elif defined(__APPLE__)
    char buffer[4096];
    std::uint32_t size = sizeof buffer;
    return _NSGetExecutablePath(buffer, &size) == 0 ? make_absolute_normal_path(buffer) : std::string("prk");
#else
    std::error_code error;
    const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::string("prk") : format_file_path(path);
#endif
}

}  // namespace

TestSuiteServices make_system_test_suite_services() {
    TestSuiteServices services;
    services.process_runner = prk_get_system_process_runner();
    services.read_file = &read_whole_file;
    services.write_file = &write_whole_file;
    services.path_exists = &does_path_exist;
    services.find_own_executable = &find_running_executable;
    services.make_absolute_path = &make_absolute_normal_path;
#if defined(_WIN32)
    services.path_separator = '\\';
    services.executable_suffix = ".exe";
    services.os_name = "windows";
#elif defined(__APPLE__)
    services.os_name = "macos";
#endif
    return services;
}

}  // namespace prk
