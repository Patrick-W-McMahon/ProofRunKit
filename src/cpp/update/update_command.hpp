/**
 * @file update_command.hpp
 * @brief `prk -update` and the release line of `prk -version` (#75): look up the latest GitHub release,
 *        compare it with this build, download the x64 MSI, verify its SHA-256, and start msiexec.
 *
 * Every side effect goes through UpdateServices, so the self-test drives all paths with fakes and never
 * touches the network. make_system_update_services() returns the real ones (WinHTTP on Windows).
 */
#pragma once

#include <functional>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

namespace prk {

/** @brief GitHub API address of the latest release. */
inline constexpr std::string_view kLatestReleaseUrl =
    "https://api.github.com/repos/Patrick-W-McMahon/ProofRunKit/releases/latest";

/** @brief Exit code of a failed update (same value as kExitUpdateFailed in command_line_app.hpp). */
inline constexpr int kUpdateFailedExitCode = 6;

/** @brief Result of one HTTP GET. */
struct HttpResponse {
    int status_code = 0;  ///< HTTP status; 0 when the request did not complete.
    std::string body;     ///< Response body (binary safe).
    std::string error;    ///< Non-empty when the request failed before a status arrived.
};

/** @brief Side effects of the updater. An empty function means "not available on this platform". */
struct UpdateServices {
    /** @brief GETs @p url with the Accept header @p accept, following redirects. */
    std::function<HttpResponse(const std::string& url, std::string_view accept)> http_get;
    /** @brief Returns the folder for the download (%TEMP%), or "" when there is none. */
    std::function<std::string()> find_download_directory;
    /** @brief Writes @p bytes to @p path, replacing it. @return "" or the reason it failed. */
    std::function<std::string(const std::string& path, const std::string& bytes)> write_file;
    /** @brief Starts the installer detached (msiexec /i PATH /passive). @return "" or the reason it failed. */
    std::function<std::string(const std::string& msi_path)> launch_installer;
    /** @brief Reads the user's answer to a yes/no question (one line). */
    std::function<std::string()> read_answer;
    std::string unsupported_reason;  ///< Shown when http_get or launch_installer is empty.
    char path_separator = '\\';      ///< Joins the download folder and the file name.
};

/** @brief Options of `prk -update`. */
struct UpdateOptions {
    bool is_check_only = false;   ///< -check / --check: report only.
    bool is_assumed_yes = false;  ///< -yes / --yes: do not ask before installing.
    bool is_dry_run = false;      ///< -dry-run / --dry-run: download and verify, do not start the installer.
};

/**
 * @brief Runs `prk -update`.
 * @param program_name Name used in messages ("prk"). @param options Parsed options.
 * @param services Side effects. @param output Progress and results. @param errors Failure reasons.
 * @return 0 when up to date, checked, cancelled, dry-run, or the installer started; kUpdateFailedExitCode on any
 *         failure (no release, network, missing or mismatched checksum, unsupported platform).
 *         Side effects: only through @p services.
 */
[[nodiscard]] int run_update_command(std::string_view program_name, const UpdateOptions& options,
                                     const UpdateServices& services, std::ostream& output, std::ostream& errors);

/**
 * @brief Writes the release line of `prk -version`: "Latest release: X (up to date)", "... update available,
 *        run 'prk -update'", or "Could not check for updates: REASON". Never fails.
 * @param program_name Name used in the hint. @param services Side effects (only http_get is used).
 * @param output Stream for the line. @return Nothing. Side effects: one HTTP request through @p services.
 */
void write_release_status(std::string_view program_name, const UpdateServices& services, std::ostream& output);

/**
 * @brief Returns the real services: WinHTTP, %TEMP%, and msiexec on Windows x64; elsewhere http_get (and on
 *        non-x64 Windows launch_installer) are empty with unsupported_reason set.
 * @param input Stream the confirmation is read from. @return The services (they keep a reference to @p input).
 */
[[nodiscard]] UpdateServices make_system_update_services(std::istream& input);

/**
 * @brief Reports whether the environment variable PRK_OFFLINE asks for no network (set, non-empty, not "0").
 * @return True when offline. Side effects: reads the environment.
 */
[[nodiscard]] bool is_offline_requested_by_environment();

}  // namespace prk
