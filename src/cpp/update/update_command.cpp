/**
 * @file update_command.cpp
 * @brief Implementation of `prk -update` and the `prk -version` release line (#75).
 */
#include "update_command.hpp"

#include <optional>

#include "proofrunkit/prk.h"
#include "proofrunkit/prk_sha256.h"
#include "release_info.hpp"

namespace prk {

namespace {

/** @brief Latest release, or the reason it could not be read. */
struct ReleaseLookup {
    std::optional<LatestRelease> release;
    std::string problem;
};

/** @brief GETs and parses the latest release. */
ReleaseLookup look_up_latest_release(const UpdateServices& services) {
    if (!services.http_get) {
        return {std::nullopt, services.unsupported_reason};
    }
    const HttpResponse response = services.http_get(std::string(kLatestReleaseUrl), "application/vnd.github+json");
    if (!response.error.empty()) {
        return {std::nullopt, response.error};
    }
    if (response.status_code == 404) {
        return {std::nullopt, "no release has been published yet"};
    }
    if (response.status_code != 200) {
        return {std::nullopt, "GitHub answered HTTP " + std::to_string(response.status_code)};
    }
    std::string problem;
    std::optional<LatestRelease> release = parse_latest_release(response.body, problem);
    return {std::move(release), problem};
}

/** @brief Version of this build (0.0.0 if it does not parse). */
ReleaseVersion read_running_version() {
    return parse_release_version(prk_get_version_string()).value_or(ReleaseVersion{});
}

/** @brief Downloads @p asset into @p out_bytes. @return "" or the reason. */
std::string download_asset(const UpdateServices& services, const ReleaseAsset& asset, std::string& out_bytes) {
    const HttpResponse response = services.http_get(asset.url, "application/octet-stream");
    if (!response.error.empty()) {
        return "could not download " + asset.name + ": " + response.error;
    }
    if (response.status_code != 200) {
        return "could not download " + asset.name + ": HTTP " + std::to_string(response.status_code);
    }
    out_bytes = response.body;
    return {};
}

/** @brief Downloads the MSI and its checksum and compares them. @return "" or the reason to refuse. */
std::string download_verified_installer(const UpdateServices& services, const InstallerAssets& assets,
                                        std::string& out_installer, std::ostream& output) {
    if (!assets.has_checksum) {
        return "refusing to install: the release has no " + assets.installer.name + ".sha256 checksum";
    }
    std::string checksum_text;
    std::string problem = download_asset(services, assets.checksum, checksum_text);
    if (!problem.empty()) {
        return "refusing to install: " + problem;
    }
    const std::optional<std::string> expected = parse_sha256_file(checksum_text);
    if (!expected) {
        return "refusing to install: " + assets.checksum.name + " does not hold a SHA-256 digest";
    }
    output << "Downloading " << assets.installer.name << "...\n";
    problem = download_asset(services, assets.installer, out_installer);
    if (!problem.empty()) {
        return problem;
    }
    char actual[PRK_SHA256_HEX_LENGTH + 1];
    prk_format_sha256_hex(out_installer.data(), out_installer.size(), actual);
    if (*expected != actual) {
        return "refusing to install: SHA-256 mismatch for " + assets.installer.name + " (expected " + *expected +
               ", got " + actual + ")";
    }
    output << "SHA-256 verified: " << actual << "\n";
    return {};
}

/** @brief Reports whether @p answer (trailing blanks and CR ignored) is y or yes, in any case. */
bool is_yes(std::string_view answer) {
    const std::size_t end = answer.find_last_not_of(" \r\t");
    const std::string_view word = end == std::string_view::npos ? std::string_view() : answer.substr(0, end + 1);
    return word == "y" || word == "Y" || word == "yes" || word == "Yes" || word == "YES";
}

/** @brief Confirms, writes, and launches. @return The exit code. */
int install_downloaded(const UpdateOptions& options, const UpdateServices& services, const std::string& version,
                       const std::string& path, const std::string& bytes, std::ostream& output, std::ostream& errors) {
    if (options.is_dry_run) {
        output << "Dry run: would save " << path << " and run msiexec /i \"" << path << "\" /passive\n";
        return 0;
    }
    if (!options.is_assumed_yes) {
        output << "Install ProofRunKit " << version << " now? [y/N] " << std::flush;
        if (!services.read_answer || !is_yes(services.read_answer())) {
            output << "\nUpdate cancelled.\n";
            return 0;
        }
    }
    std::string problem = services.write_file(path, bytes);
    if (problem.empty()) {
        problem = services.launch_installer(path);
    }
    if (!problem.empty()) {
        errors << "update failed: " << problem << "\n";
        return kUpdateFailedExitCode;
    }
    output << "Started the installer (msiexec /passive). ProofRunKit exits now so it can be replaced.\n";
    return 0;
}

/** @brief Fails with @p message on @p errors. */
int fail_update(std::string_view program_name, const std::string& message, std::ostream& errors) {
    errors << program_name << " -update: " << message << "\n";
    return kUpdateFailedExitCode;
}

}  // namespace

int run_update_command(std::string_view program_name, const UpdateOptions& options, const UpdateServices& services,
                       std::ostream& output, std::ostream& errors) {
    const ReleaseLookup lookup = look_up_latest_release(services);
    if (!lookup.release) {
        return fail_update(program_name, "could not check for updates: " + lookup.problem, errors);
    }
    const LatestRelease& release = *lookup.release;
    const std::string latest = format_release_version(release.version);
    output << "Current version: " << prk_get_version_string() << "\nLatest release:  " << latest << " (" << release.tag
           << ")\n";
    if (compare_release_versions(release.version, read_running_version()) <= 0) {
        output << "ProofRunKit is up to date.\n";
        return 0;
    }
    if (options.is_check_only) {
        output << "An update is available. Run '" << program_name << " -update' to install it.\n";
        return 0;
    }
    if (!services.launch_installer || !services.write_file || !services.find_download_directory) {
        return fail_update(program_name, services.unsupported_reason, errors);
    }
    const std::optional<InstallerAssets> assets = choose_installer_assets(release);
    if (!assets) {
        return fail_update(program_name, "release " + release.tag + " has no x64 MSI (ProofRunKit-*x64*.msi)", errors);
    }
    const std::string folder = services.find_download_directory();
    if (folder.empty()) {
        return fail_update(program_name, "no download folder (%TEMP% is not set)", errors);
    }
    std::string installer;
    const std::string problem = download_verified_installer(services, *assets, installer, output);
    if (!problem.empty()) {
        return fail_update(program_name, problem, errors);
    }
    const std::string path = folder + services.path_separator + assets->installer.name;
    return install_downloaded(options, services, latest, path, installer, output, errors);
}

void write_release_status(std::string_view program_name, const UpdateServices& services, std::ostream& output) {
    const ReleaseLookup lookup = look_up_latest_release(services);
    if (!lookup.release) {
        output << "Could not check for updates: " << lookup.problem << "\n";
        return;
    }
    const std::string latest = format_release_version(lookup.release->version);
    const int order = compare_release_versions(lookup.release->version, read_running_version());
    if (order > 0) {
        output << "Latest release: " << latest << " - update available, run '" << program_name << " -update'\n";
    } else {
        output << "Latest release: " << latest << (order == 0 ? " - up to date\n" : " - this build is newer\n");
    }
}

}  // namespace prk
