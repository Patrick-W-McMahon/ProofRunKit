/**
 * @file script_host.cpp
 * @brief Implementation of the script host services (#12). Only make_disk_script_host_services touches disk.
 */
#include "script_host.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace prk {

bool is_script_data_path_allowed(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\' ||
        path.find_first_of(std::string_view(":\0", 2)) != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find_first_of("/\\", start), path.size());
        if (path.substr(start, end - start) == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

ScriptHostServices make_disk_script_host_services() {
    ScriptHostServices services;
    services.read_monotonic_milliseconds = [] {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    };
    services.find_file = [](const std::string& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(std::filesystem::path(path), error) ? PRK_FILE_ACCESS_OK
                                                                                    : PRK_FILE_ACCESS_NOT_FOUND;
    };
    services.process_runner = prk_get_system_process_runner();
    services.read_file = [](const std::string& path, std::string& out_bytes) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(std::filesystem::path(path), error)) {
            return PRK_FILE_ACCESS_NOT_FOUND;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return PRK_FILE_ACCESS_READ_ERROR;
        }
        std::ostringstream bytes;
        bytes << file.rdbuf();
        if (file.bad()) {
            return PRK_FILE_ACCESS_READ_ERROR;
        }
        out_bytes = bytes.str();
        return PRK_FILE_ACCESS_OK;
    };
    return services;
}

ScriptHost::ScriptHost(ScriptHostServices services, std::ostream& print_output)
    : services_(std::move(services)), print_output_(print_output) {}

ScriptHost::~ScriptHost() {
    prk_release_msvc_toolchain(&msvc_toolchain_);
}

PrkScriptHost ScriptHost::make_c_host() {
    return PrkScriptHost{&ScriptHost::write_text,
                         services_.read_monotonic_milliseconds ? &ScriptHost::read_monotonic_milliseconds : nullptr,
                         services_.find_file ? &ScriptHost::find_file : nullptr,
                         services_.read_file ? &ScriptHost::read_file : nullptr,
                         this,
                         services_.process_runner,
                         &msvc_toolchain_};
}

void ScriptHost::write_text(void* host_state, const char* text, std::size_t length) {
    try {
        static_cast<ScriptHost*>(host_state)->print_output_.write(text, static_cast<std::streamsize>(length));
    } catch (...) {
        // print() output is best effort; a failing stream must not unwind through Lua.
    }
}

std::uint64_t ScriptHost::read_monotonic_milliseconds(void* host_state) {
    try {
        return static_cast<ScriptHost*>(host_state)->services_.read_monotonic_milliseconds();
    } catch (...) {
        return 0;
    }
}

PrkFileAccessStatus ScriptHost::find_file(void* host_state, const char* path) {
    if (!is_script_data_path_allowed(path)) {
        return PRK_FILE_ACCESS_DENIED;
    }
    try {
        return static_cast<ScriptHost*>(host_state)->services_.find_file(path);
    } catch (...) {
        return PRK_FILE_ACCESS_READ_ERROR;
    }
}

PrkFileAccessStatus ScriptHost::read_file(void* host_state, const char* path, PrkReceiveFileBytes receive_bytes,
                                          void* sink) {
    if (!is_script_data_path_allowed(path)) {
        return PRK_FILE_ACCESS_DENIED;
    }
    try {
        std::string bytes;
        const PrkFileAccessStatus status = static_cast<ScriptHost*>(host_state)->services_.read_file(path, bytes);
        if (status == PRK_FILE_ACCESS_OK) {
            receive_bytes(sink, bytes.data(), bytes.size());
        }
        return status;
    } catch (...) {
        return PRK_FILE_ACCESS_READ_ERROR;
    }
}

}  // namespace prk
