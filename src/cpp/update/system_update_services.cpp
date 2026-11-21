/**
 * @file system_update_services.cpp
 * @brief The real UpdateServices (#75): WinHTTP, %TEMP%, a binary file writer, msiexec, and stdin answers.
 */
#include <array>
#include <fstream>

#include "proofrunkit/prk_process.h"
#include "update_command.hpp"
#if defined(_WIN32)
#include "system_update_windows.hpp"
#endif

namespace prk {

namespace {

/** @brief Writes @p bytes to @p path in binary mode. @return "" or the reason. */
std::string write_binary_file(const std::string& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return "cannot open " + path + " for writing";
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return file ? std::string() : "cannot write " + path;
}

/** @brief Returns %TEMP% (or %TMP%), or "". */
std::string find_temporary_directory() {
    std::array<char, 1024> value{};
    for (const char* const name : {"TEMP", "TMP"}) {
        if (prk_read_environment_variable(name, value.data(), value.size()) && value[0] != '\0') {
            return value.data();
        }
    }
    return {};
}

}  // namespace

UpdateServices make_system_update_services(std::istream& input) {
    UpdateServices services;
    services.read_answer = [&input]() {
        std::string answer;
        std::getline(input, answer);
        return answer;
    };
#if defined(_WIN32)
    services.http_get = &get_with_winhttp;
#if defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
    services.find_download_directory = &find_temporary_directory;
    services.write_file = &write_binary_file;
    services.launch_installer = &launch_msiexec_detached;
#else
    services.unsupported_reason = "only the Windows x64 build can install updates (#74); download the MSI instead";
#endif
#else
    services.unsupported_reason = "updates are not supported on this platform yet (Windows x64 only)";
    services.path_separator = '/';
#endif
    (void)&find_temporary_directory; /* Used only on Windows x64. */
    (void)&write_binary_file;
    return services;
}

bool is_offline_requested_by_environment() {
    std::array<char, 64> value{};
    return prk_read_environment_variable("PRK_OFFLINE", value.data(), value.size()) && value[0] != '\0' &&
           std::string_view(value.data()) != "0";
}

}  // namespace prk
