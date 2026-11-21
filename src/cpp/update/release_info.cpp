/**
 * @file release_info.cpp
 * @brief Implementation of the pure `prk -update` release helpers (#75).
 */
#include "release_info.hpp"

#include <algorithm>
#include <cctype>

#include "json_reader.hpp"

namespace prk {

namespace {

/** @brief Reads digits at @p position into @p out_number; false when there are none or it overflows. */
bool read_version_number(std::string_view text, std::size_t& position, unsigned& out_number) {
    const std::size_t start = position;
    unsigned long value = 0;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9' && position - start < 9) {
        value = value * 10 + static_cast<unsigned long>(text[position] - '0');
        ++position;
    }
    out_number = static_cast<unsigned>(value);
    return position > start && (position >= text.size() || !std::isdigit(static_cast<unsigned char>(text[position])));
}

std::string make_lowercase(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered;
}

}  // namespace

std::optional<ReleaseVersion> parse_release_version(std::string_view text) {
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) {
        text.remove_prefix(1);
    }
    text = text.substr(0, text.find('+'));
    ReleaseVersion version;
    std::size_t position = 0;
    if (!read_version_number(text, position, version.major) || position >= text.size() || text[position++] != '.' ||
        !read_version_number(text, position, version.minor) || position >= text.size() || text[position++] != '.' ||
        !read_version_number(text, position, version.patch)) {
        return std::nullopt;
    }
    if (position < text.size()) {
        if (text[position] != '-' || position + 1 >= text.size()) {
            return std::nullopt;
        }
        version.prerelease = std::string(text.substr(position + 1));
    }
    return version;
}

int compare_release_versions(const ReleaseVersion& left, const ReleaseVersion& right) {
    if (left.major != right.major) {
        return left.major < right.major ? -1 : 1;
    }
    if (left.minor != right.minor) {
        return left.minor < right.minor ? -1 : 1;
    }
    if (left.patch != right.patch) {
        return left.patch < right.patch ? -1 : 1;
    }
    if (left.prerelease.empty() != right.prerelease.empty()) {
        return left.prerelease.empty() ? 1 : -1;
    }
    return left.prerelease.compare(right.prerelease) < 0 ? -1 : (left.prerelease == right.prerelease ? 0 : 1);
}

std::string format_release_version(const ReleaseVersion& version) {
    std::string text =
        std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
    return version.prerelease.empty() ? text : text + "-" + version.prerelease;
}

std::optional<LatestRelease> parse_latest_release(std::string_view json_text, std::string& out_error) {
    const std::optional<JsonValue> document = parse_json_document(json_text, out_error);
    if (!document) {
        out_error = "the release document is not JSON (" + out_error + ")";
        return std::nullopt;
    }
    LatestRelease release;
    release.tag = document->read_string_member("tag_name");
    const std::optional<ReleaseVersion> version = parse_release_version(release.tag);
    if (!version) {
        out_error = "the latest release tag '" + release.tag + "' is not a version";
        return std::nullopt;
    }
    release.version = *version;
    release.page_url = document->read_string_member("html_url");
    const JsonValue* const assets = document->find_member("assets");
    if (assets != nullptr && assets->kind == JsonValue::Kind::Array) {
        for (const JsonValue& asset : assets->items) {
            release.assets.push_back(
                ReleaseAsset{asset.read_string_member("name"), asset.read_string_member("browser_download_url")});
        }
    }
    return release;
}

bool is_safe_asset_name(std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 200) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '.' || character == '_' ||
               character == '-';
    });
}

std::optional<InstallerAssets> choose_installer_assets(const LatestRelease& release) {
    for (const ReleaseAsset& asset : release.assets) {
        const std::string lowered = make_lowercase(asset.name);
        if (lowered.starts_with("proofrunkit-") && lowered.find("x64") != std::string::npos &&
            lowered.ends_with(".msi") && is_safe_asset_name(asset.name) && asset.url.starts_with("https://")) {
            InstallerAssets chosen{asset, ReleaseAsset{}, false};
            for (const ReleaseAsset& candidate : release.assets) {
                if (make_lowercase(candidate.name) == lowered + ".sha256" && candidate.url.starts_with("https://")) {
                    chosen.checksum = candidate;
                    chosen.has_checksum = true;
                }
            }
            return chosen;
        }
    }
    return std::nullopt;
}

std::optional<std::string> parse_sha256_file(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3); /* A UTF-8 byte order mark, as some editors write. */
    }
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    if (text.size() - start < 64) {
        return std::nullopt;
    }
    const std::string_view digest = text.substr(start, 64);
    const bool is_hex = std::all_of(digest.begin(), digest.end(), [](char character) {
        return std::isxdigit(static_cast<unsigned char>(character));
    });
    const bool is_ended = start + 64 == text.size() || std::isspace(static_cast<unsigned char>(text[start + 64])) != 0;
    if (!is_hex || !is_ended) {
        return std::nullopt;
    }
    return make_lowercase(digest);
}

}  // namespace prk
