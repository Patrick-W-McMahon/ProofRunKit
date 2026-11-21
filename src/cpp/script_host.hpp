/**
 * @file script_host.hpp
 * @brief The services the front end grants to test scripts (#12): print output, a monotonic clock,
 *        and read-only access to relative files under the working directory.
 *
 * ScriptHost adapts injectable std::function services to the C PrkScriptHost callbacks and applies
 * the path policy (is_script_data_path_allowed) before any file service runs, so fakes in tests get
 * the same policy as the disk. The only real IO is in make_disk_script_host_services().
 */
#pragma once

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>

#include "proofrunkit/prk_script.h"

namespace prk {

/** @brief Injectable services behind PrkScriptHost. An empty function disables that capability. */
struct ScriptHostServices {
    std::function<std::uint64_t()> read_monotonic_milliseconds;             ///< Clock for timeouts/timing.
    std::function<PrkFileAccessStatus(const std::string& path)> find_file;  ///< OK or NOT_FOUND.
    std::function<PrkFileAccessStatus(const std::string& path, std::string& out_bytes)> read_file;  ///< Whole file.
    PrkProcessRunner process_runner{};  ///< Process spawn for scripts (#12, #13); spawn_process NULL disables it.
};

/**
 * @brief Reports whether a script may read @p path: relative, non-empty, no "..", no drive or stream
 *        separator ':', no leading slash or backslash, no NUL. Pure function.
 * @param path Path exactly as the script passed it.
 * @return True when the path stays inside the working directory (symbolic links are not resolved).
 */
[[nodiscard]] bool is_script_data_path_allowed(std::string_view path);

/**
 * @brief Services backed by std::chrono::steady_clock and std::filesystem, relative to the current directory.
 * @return Services suitable for main(). Their callbacks perform real (read-only) IO.
 */
[[nodiscard]] ScriptHostServices make_disk_script_host_services();

/** @brief Owns the state behind one PrkScriptHost. Must outlive every script created with make_c_host(). */
class ScriptHost {
public:
    /**
     * @param services Clock and file services (copied).
     * @param print_output Stream that receives script print() text. Not owned.
     */
    ScriptHost(ScriptHostServices services, std::ostream& print_output);

    /** @brief Returns C callbacks that point at this object. Callbacks never throw. */
    [[nodiscard]] PrkScriptHost make_c_host();

private:
    static void write_text(void* host_state, const char* text, std::size_t length);
    static std::uint64_t read_monotonic_milliseconds(void* host_state);
    static PrkFileAccessStatus find_file(void* host_state, const char* path);
    static PrkFileAccessStatus read_file(void* host_state, const char* path, PrkReceiveFileBytes receive_bytes,
                                         void* sink);

    ScriptHostServices services_;
    std::ostream& print_output_;
};

}  // namespace prk
