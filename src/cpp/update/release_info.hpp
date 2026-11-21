/**
 * @file release_info.hpp
 * @brief Pure helpers for `prk -update` (#75): release versions, the GitHub "latest release" document, the
 *        choice of the x64 MSI and its .sha256 asset, and checksum-file parsing. No IO.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prk {

/** @brief A MAJOR.MINOR.PATCH version with an optional pre-release tag ("1.2.0-rc.1"). */
struct ReleaseVersion {
    unsigned major = 0;      ///< Major number.
    unsigned minor = 0;      ///< Minor number.
    unsigned patch = 0;      ///< Patch number.
    std::string prerelease;  ///< Text after '-', or empty for a release.
};

/** @brief One downloadable file of a release. */
struct ReleaseAsset {
    std::string name;  ///< File name, e.g. "ProofRunKit-0.2.0-win-x64.msi".
    std::string url;   ///< browser_download_url.
};

/** @brief The fields of a GitHub release that the updater uses. */
struct LatestRelease {
    std::string tag;                   ///< tag_name, e.g. "v0.2.0".
    ReleaseVersion version;            ///< Parsed from tag.
    std::string page_url;              ///< html_url, shown to the user.
    std::vector<ReleaseAsset> assets;  ///< Every asset in document order.
};

/** @brief The installer and its checksum chosen from a release. */
struct InstallerAssets {
    ReleaseAsset installer;     ///< The x64 MSI.
    ReleaseAsset checksum;      ///< "<installer name>.sha256", when the release has it.
    bool has_checksum = false;  ///< False when the release lacks the .sha256 asset.
};

/**
 * @brief Parses "1.2.3", "v1.2.3", or "1.2.3-rc.1" (build metadata after '+' is ignored). Pure function.
 * @param text Version or tag. @return The version, or std::nullopt when it is not in that form.
 */
[[nodiscard]] std::optional<ReleaseVersion> parse_release_version(std::string_view text);

/**
 * @brief Orders two versions: numbers first, then a release after any pre-release of the same numbers, then
 *        pre-release tags compared as text. Pure function.
 * @param left First version. @param right Second version. @return Negative, zero, or positive.
 */
[[nodiscard]] int compare_release_versions(const ReleaseVersion& left, const ReleaseVersion& right);

/**
 * @brief Formats a version as "1.2.3" or "1.2.3-rc.1". Pure function.
 * @param version Version. @return The text.
 */
[[nodiscard]] std::string format_release_version(const ReleaseVersion& version);

/**
 * @brief Reads the GitHub "latest release" JSON. Pure function.
 * @param json_text Response body. @param out_error Receives the reason on failure.
 * @return The release, or std::nullopt when the body is not JSON or tag_name is not a version.
 */
[[nodiscard]] std::optional<LatestRelease> parse_latest_release(std::string_view json_text, std::string& out_error);

/**
 * @brief Reports whether @p name is a plain file name (letters, digits, '.', '_', '-'; not "." or "..").
 *        Release asset names are checked with this before they become paths. Pure function.
 * @param name Candidate name. @return True when safe.
 */
[[nodiscard]] bool is_safe_asset_name(std::string_view name);

/**
 * @brief Picks the x64 MSI (name starts with "ProofRunKit-", contains "x64", ends ".msi"; any case) and
 *        "<msi name>.sha256". Pure function.
 * @param release Release. @return The assets, or std::nullopt when there is no safe x64 MSI.
 */
[[nodiscard]] std::optional<InstallerAssets> choose_installer_assets(const LatestRelease& release);

/**
 * @brief Reads a checksum file: 64 hex digits, optionally followed by white space and a file name
 *        ("<hex>  name" or "<hex> *name"; a leading UTF-8 BOM is skipped), as written by installer/build-msi-x64.ps1
 * and sha256sum. Pure function.
 * @param text File contents. @return The lower-case digest, or std::nullopt when there is none.
 */
[[nodiscard]] std::optional<std::string> parse_sha256_file(std::string_view text);

}  // namespace prk
