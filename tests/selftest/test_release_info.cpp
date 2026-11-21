/**
 * @file test_release_info.cpp
 * @brief Tests for the pure parts of `prk -update` (#75): SHA-256 (FIPS 180-2 vectors), the JSON reader,
 *        release versions, the GitHub release document, the MSI / .sha256 asset choice, and checksum files.
 */
#include <array>
#include <cstring>
#include <string>

#include "cpp/update/json_reader.hpp"
#include "cpp/update/release_info.hpp"
#include "proofrunkit/prk_sha256.h"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

std::string hash_text(const std::string& text) {
    std::array<char, PRK_SHA256_HEX_LENGTH + 1> hex{};
    prk_format_sha256_hex(text.data(), text.size(), hex.data());
    return hex.data();
}

void test_hashes_sha256_vectors(PrkTestContext* context, void*) {
    PRK_EXPECT(hash_text("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    PRK_EXPECT(hash_text("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    PRK_EXPECT(hash_text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    PRK_EXPECT(hash_text(std::string(1000000, 'a')) ==
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    PrkSha256 hash;
    prk_start_sha256(&hash);
    const std::string text = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for (const char character : text) {
        prk_update_sha256(&hash, &character, 1);
    }
    prk_update_sha256(&hash, nullptr, 0);
    std::array<unsigned char, PRK_SHA256_DIGEST_SIZE> digest{};
    prk_finish_sha256(&hash, digest.data());
    PRK_EXPECT(digest[0] == 0x24 && digest[31] == 0xc1);
}

void test_reads_json(PrkTestContext* context, void*) {
    std::string error;
    const auto value = parse_json_document(
        " {\"a\": [1, -2.5e3, true, false, null], \"s\": \"x\\\"\\\\\\/\\n\\u00e9\\ud83d\\ude00\", \"o\": {}} ", error);
    PRK_EXPECT(value && value->kind == JsonValue::Kind::Object && error.empty());
    const JsonValue* const array = value ? value->find_member("a") : nullptr;
    PRK_EXPECT(array != nullptr && array->items.size() == 5 && array->items[1].text == "-2.5e3");
    PRK_EXPECT(array != nullptr && array->items[2].boolean && array->items[4].kind == JsonValue::Kind::Null);
    PRK_EXPECT(value && value->read_string_member("s") == "x\"\\/\n\xC3\xA9\xF0\x9F\x98\x80");
    PRK_EXPECT(value && value->read_string_member("a").empty() && value->find_member("missing") == nullptr);
    for (const char* const bad : {"", "{", "[1,]", "{\"a\" 1}", "\"\\q\"", "tru", "1 2", "\"a\nb\"", "{1: 2}"}) {
        PRK_EXPECT(!parse_json_document(bad, error) && !error.empty());
    }
    PRK_EXPECT(!parse_json_document(std::string(100, '['), error) && error.find("deep") != std::string::npos);
}

void test_compares_release_versions(PrkTestContext* context, void*) {
    const auto parse = [](const char* text) {
        return parse_release_version(text).value_or(ReleaseVersion{99, 0, 0, ""});
    };
    PRK_EXPECT(format_release_version(parse("v1.2.3")) == "1.2.3");
    PRK_EXPECT(format_release_version(parse("0.2.0-rc.1+build5")) == "0.2.0-rc.1");
    PRK_EXPECT(compare_release_versions(parse("0.1.0"), parse("v0.2.0")) < 0);
    PRK_EXPECT(compare_release_versions(parse("1.10.0"), parse("1.9.9")) > 0);
    PRK_EXPECT(compare_release_versions(parse("1.0.0-rc.1"), parse("1.0.0")) < 0);
    PRK_EXPECT(compare_release_versions(parse("1.0.0-beta"), parse("1.0.0-alpha")) > 0);
    PRK_EXPECT(compare_release_versions(parse("V2.0.0"), parse("2.0.0")) == 0);
    for (const char* const bad : {"", "1", "1.2", "1.2.x", "1.2.3-", "1..3", "v", "1.2.3.4", "1234567890.0.0"}) {
        PRK_EXPECT(!parse_release_version(bad));
    }
}

constexpr const char* kReleaseJson =
    "{\"tag_name\": \"v0.2.0\", \"html_url\": \"https://github.com/o/r/releases/tag/v0.2.0\", \"assets\": ["
    "{\"name\": \"ProofRunKit-0.2.0-win-x86.msi\", \"browser_download_url\": \"https://h/x86.msi\"},"
    "{\"name\": \"ProofRunKit-0.2.0-win-x64.msi.sha256\", \"browser_download_url\": \"https://h/x64.sha\"},"
    "{\"name\": \"ProofRunKit-0.2.0-win-x64.msi\", \"browser_download_url\": \"https://h/x64.msi\"}]}";

void test_reads_release_and_chooses_assets(PrkTestContext* context, void*) {
    std::string error;
    const auto release = parse_latest_release(kReleaseJson, error);
    PRK_EXPECT(release && release->tag == "v0.2.0" && release->assets.size() == 3 && release->version.minor == 2);
    PRK_EXPECT(release && release->page_url == "https://github.com/o/r/releases/tag/v0.2.0");
    const auto assets = release ? choose_installer_assets(*release) : std::nullopt;
    PRK_EXPECT(assets && assets->installer.url == "https://h/x64.msi" && assets->has_checksum &&
               assets->checksum.url == "https://h/x64.sha");
    LatestRelease without = *release;
    without.assets.erase(without.assets.begin() + 1);
    const auto unchecked = choose_installer_assets(without);
    PRK_EXPECT(unchecked && !unchecked->has_checksum);
    without.assets = {ReleaseAsset{"ProofRunKit-x64-0.2.0.msi", "http://plain/text"},
                      ReleaseAsset{"..\\ProofRunKit-x64.msi", "https://h/evil"}};
    PRK_EXPECT(!choose_installer_assets(without));
    without.assets = {ReleaseAsset{"ProofRunKit-x64-0.2.0.msi", "https://h/a"}};
    PRK_EXPECT(choose_installer_assets(without).has_value());
    PRK_EXPECT(!parse_latest_release("{\"tag_name\": \"nightly\"}", error) &&
               error.find("nightly") != std::string::npos);
    PRK_EXPECT(!parse_latest_release("<html>", error) && error.find("not JSON") != std::string::npos);
    PRK_EXPECT(is_safe_asset_name("a-b_c.1.msi") && !is_safe_asset_name("..") && !is_safe_asset_name("a/b"));
    PRK_EXPECT(!is_safe_asset_name("") && !is_safe_asset_name("a b") && !is_safe_asset_name("C:x"));
}

void test_parses_sha256_files(PrkTestContext* context, void*) {
    const std::string digest = "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855";
    const std::string lowered = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    PRK_EXPECT(parse_sha256_file(digest) == lowered);
    PRK_EXPECT(parse_sha256_file("\xEF\xBB\xBF" + digest) == lowered);
    PRK_EXPECT(parse_sha256_file("  " + digest + "  ProofRunKit-0.2.0-win-x64.msi\r\n") == lowered);
    PRK_EXPECT(parse_sha256_file(lowered + " *ProofRunKit.msi") == lowered);
    PRK_EXPECT(!parse_sha256_file(lowered.substr(1)) && !parse_sha256_file(lowered + "0"));
    PRK_EXPECT(!parse_sha256_file("z" + lowered.substr(1)) && !parse_sha256_file(""));
}

}  // namespace

PrkStatus register_release_info_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"update.hashes_sha256_vectors", &test_hashes_sha256_vectors, nullptr},
        {"update.reads_json", &test_reads_json, nullptr},
        {"update.compares_release_versions", &test_compares_release_versions, nullptr},
        {"update.reads_release_and_chooses_assets", &test_reads_release_and_chooses_assets, nullptr},
        {"update.parses_sha256_files", &test_parses_sha256_files, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
