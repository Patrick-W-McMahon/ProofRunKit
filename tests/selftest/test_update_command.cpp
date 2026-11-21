/**
 * @file test_update_command.cpp
 * @brief Tests for `prk -update` and the `prk -version` release line (#75) with fake UpdateServices: parsing,
 *        up to date, check only, dry run, install, declined, checksum missing / mismatched, no release, network
 *        errors, and unsupported platforms. Nothing here touches the network.
 */
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cpp/command_line.hpp"
#include "cpp/command_line_app.hpp"
#include "cpp/update/update_command.hpp"
#include "proofrunkit/prk_sha256.h"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

constexpr const char* kInstallerBytes = "MSI bytes for the fake 9.0.0 release";

/** @brief Fake services: canned responses by URL; records downloads, writes, and launches. */
struct FakeUpdate {
    std::map<std::string, HttpResponse> responses;
    std::vector<std::string> requested;
    std::vector<std::string> written;
    std::vector<std::string> launched;
    std::string answer = "y";

    FakeUpdate() {
        std::array<char, PRK_SHA256_HEX_LENGTH + 1> digest{};
        prk_format_sha256_hex(kInstallerBytes, std::strlen(kInstallerBytes), digest.data());
        set_release("v9.0.0", true);
        responses["https://h/x64.msi"] = HttpResponse{200, kInstallerBytes, ""};
        responses["https://h/x64.sha256"] = HttpResponse{200, std::string(digest.data()) + "  x64.msi\n", ""};
    }

    void set_release(const std::string& tag, bool has_checksum) {
        std::string assets =
            "{\"name\": \"ProofRunKit-9.0.0-win-x64.msi\", \"browser_download_url\": \"https://h/x64.msi\"}";
        if (has_checksum) {
            assets +=
                ",{\"name\": \"ProofRunKit-9.0.0-win-x64.msi.sha256\", \"browser_download_url\": "
                "\"https://h/x64.sha256\"}";
        }
        responses[std::string(kLatestReleaseUrl)] =
            HttpResponse{200, "{\"tag_name\": \"" + tag + "\", \"assets\": [" + assets + "]}", ""};
    }

    [[nodiscard]] UpdateServices make_services() {
        UpdateServices services;
        services.http_get = [this](const std::string& url, std::string_view) {
            requested.push_back(url);
            const auto found = responses.find(url);
            return found != responses.end() ? found->second : HttpResponse{404, "", ""};
        };
        services.find_download_directory = []() { return std::string("C:\\Temp"); };
        services.write_file = [this](const std::string& path, const std::string& bytes) {
            written.push_back(path + "=" + bytes);
            return std::string();
        };
        services.launch_installer = [this](const std::string& path) {
            launched.push_back(path);
            return std::string();
        };
        services.read_answer = [this]() { return answer; };
        return services;
    }
};

/** @brief Output of one run. */
struct UpdateRun {
    int exit_code;
    std::string output;
    std::string errors;
};

UpdateRun run_update(FakeUpdate& fake, const UpdateOptions& options) {
    std::ostringstream output;
    std::ostringstream errors;
    const UpdateServices services = fake.make_services();
    const int exit_code = run_update_command("prk", options, services, output, errors);
    return UpdateRun{exit_code, output.str(), errors.str()};
}

bool contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

void test_parses_update_and_version(PrkTestContext* context, void*) {
    for (const std::string_view spelling : {"-update", "--update", "update"}) {
        const std::vector<std::string_view> arguments = {spelling, "-check", "--yes", "-dry-run"};
        const CommandLineParseResult parsed = parse_command_line(arguments);
        PRK_EXPECT(parsed.error_message.empty() && parsed.options.command == CliCommand::Update);
        PRK_EXPECT(parsed.options.is_update_check_only && parsed.options.is_update_assumed_yes &&
                   parsed.options.is_update_dry_run);
    }
    const std::vector<std::string_view> bad = {"-update", "-force"};
    PRK_EXPECT(parse_command_line(bad).error_message.find("unknown update option '-force'") == 0);
    for (const std::string_view spelling : {"-version", "-v", "version"}) {
        const std::vector<std::string_view> arguments = {spelling, "-offline"};
        const CommandLineParseResult parsed = parse_command_line(arguments);
        PRK_EXPECT(parsed.error_message.empty() && parsed.options.show_version && parsed.options.is_offline);
    }
    const std::vector<std::string_view> flag_form = {"--version", "--offline"};
    PRK_EXPECT(parse_command_line(flag_form).options.is_offline);
    const std::vector<std::string_view> help = {"help", "-update"};
    PRK_EXPECT(parse_command_line(help).options.help_topic == "update");
    PRK_EXPECT(format_command_help_text("prk", "update").find("Usage: prk -update") == 0);
    PRK_EXPECT(contains(format_usage_text("prk"), "-update"));
}

void test_reports_and_checks(PrkTestContext* context, void*) {
    FakeUpdate fake;
    fake.set_release(std::string("v") + prk_get_version_string(), true);
    UpdateRun run = run_update(fake, UpdateOptions{});
    PRK_EXPECT(run.exit_code == 0 && contains(run.output, "up to date") && fake.requested.size() == 1);
    fake.set_release("v9.0.0", true);
    run = run_update(fake, UpdateOptions{true, false, false});
    PRK_EXPECT(run.exit_code == 0 && contains(run.output, "Latest release:  9.0.0 (v9.0.0)"));
    PRK_EXPECT(contains(run.output, "Run 'prk -update'") && fake.requested.size() == 2 && fake.launched.empty());
}

void test_installs_verified_update(PrkTestContext* context, void*) {
    FakeUpdate fake;
    UpdateRun run = run_update(fake, UpdateOptions{false, false, true});
    PRK_EXPECT(run.exit_code == 0 && contains(run.output, "SHA-256 verified") && contains(run.output, "Dry run"));
    PRK_EXPECT(fake.written.empty() && fake.launched.empty());
    fake.answer = "n";
    run = run_update(fake, UpdateOptions{});
    PRK_EXPECT(run.exit_code == 0 && contains(run.output, "Update cancelled") && fake.launched.empty());
    fake.answer = "Yes\r";
    run = run_update(fake, UpdateOptions{});
    PRK_EXPECT(run.exit_code == 0 && contains(run.output, "[y/N]") && contains(run.output, "Started the installer"));
    PRK_EXPECT(fake.launched == std::vector<std::string>({"C:\\Temp\\ProofRunKit-9.0.0-win-x64.msi"}));
    PRK_EXPECT(fake.written.size() == 1 && fake.written[0] == fake.launched[0] + "=" + kInstallerBytes);
    fake.answer = "";
    run = run_update(fake, UpdateOptions{false, true, false});
    PRK_EXPECT(run.exit_code == 0 && !contains(run.output, "[y/N]") && fake.launched.size() == 2);
}

void test_refuses_unverified_update(PrkTestContext* context, void*) {
    FakeUpdate fake;
    fake.responses["https://h/x64.msi"].body += "tampered";
    UpdateRun run = run_update(fake, UpdateOptions{false, true, false});
    PRK_EXPECT(run.exit_code == kUpdateFailedExitCode && contains(run.errors, "SHA-256 mismatch"));
    fake.responses["https://h/x64.sha256"].body = "not a digest";
    run = run_update(fake, UpdateOptions{false, true, false});
    PRK_EXPECT(run.exit_code == kUpdateFailedExitCode && contains(run.errors, "does not hold a SHA-256"));
    fake.responses.erase("https://h/x64.sha256");
    run = run_update(fake, UpdateOptions{false, true, false});
    PRK_EXPECT(run.exit_code == kUpdateFailedExitCode && contains(run.errors, "HTTP 404"));
    fake.set_release("v9.0.0", false);
    run = run_update(fake, UpdateOptions{false, true, false});
    PRK_EXPECT(run.exit_code == kUpdateFailedExitCode &&
               contains(run.errors, "has no ProofRunKit-9.0.0-win-x64.msi.sha256"));
    PRK_EXPECT(fake.written.empty() && fake.launched.empty());
}

void test_reports_lookup_failures(PrkTestContext* context, void*) {
    FakeUpdate fake;
    fake.responses.erase(std::string(kLatestReleaseUrl));
    UpdateRun run = run_update(fake, UpdateOptions{true, false, false});
    PRK_EXPECT(run.exit_code == kUpdateFailedExitCode && contains(run.errors, "no release has been published yet"));
    fake.responses[std::string(kLatestReleaseUrl)] =
        HttpResponse{0, "", "WinHttpSendRequest failed (Windows error 12007)"};
    run = run_update(fake, UpdateOptions{});
    PRK_EXPECT(run.exit_code == kUpdateFailedExitCode && contains(run.errors, "12007"));
    fake.responses[std::string(kLatestReleaseUrl)] = HttpResponse{403, "rate limited", ""};
    run = run_update(fake, UpdateOptions{});
    PRK_EXPECT(contains(run.errors, "GitHub answered HTTP 403"));
    UpdateServices unsupported;
    unsupported.unsupported_reason = "updates are not supported on this platform yet";
    std::ostringstream output;
    std::ostringstream errors;
    PRK_EXPECT(run_update_command("prk", UpdateOptions{}, unsupported, output, errors) == kUpdateFailedExitCode);
    PRK_EXPECT(contains(errors.str(), "not supported on this platform"));
    FakeUpdate check_only;
    UpdateServices no_install = check_only.make_services();
    no_install.launch_installer = nullptr;
    no_install.unsupported_reason = "only the Windows x64 build can install updates";
    PRK_EXPECT(run_update_command("prk", UpdateOptions{true, false, false}, no_install, output, errors) == 0);
    PRK_EXPECT(run_update_command("prk", UpdateOptions{}, no_install, output, errors) == kUpdateFailedExitCode);
    PRK_EXPECT(contains(errors.str(), "only the Windows x64 build"));
}

/** @brief Runs the whole command line with @p services. */
UpdateRun run_cli(std::vector<std::string_view> arguments, const UpdateServices* services) {
    std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)> registry(prk_create_registry(),
                                                                           &prk_destroy_registry);
    std::ostringstream output;
    std::ostringstream errors;
    const CommandLineEnvironment environment{*registry,
                                             PrkClock{nullptr, nullptr},
                                             ScriptFileReader{},
                                             ScriptHostServices{},
                                             ReportFileWriter{},
                                             output,
                                             errors,
                                             services,
                                             nullptr};
    const int exit_code = run_command_line("prk", arguments, environment);
    return UpdateRun{exit_code, output.str(), errors.str()};
}

void test_version_reports_release(PrkTestContext* context, void*) {
    FakeUpdate fake;
    const UpdateServices services = fake.make_services();
    const std::string version_line = std::string("ProofRunKit ") + prk_get_version_string() + " (cycle counter: ";
    UpdateRun run = run_cli({"-version"}, &services);
    PRK_EXPECT(run.exit_code == 0 && run.output.find(version_line) == 0);
    PRK_EXPECT(contains(run.output, "\nLatest release: 9.0.0 - update available, run 'prk -update'\n"));
    fake.set_release(std::string("v") + prk_get_version_string(), true);
    PRK_EXPECT(contains(run_cli({"-v"}, &services).output, " - up to date\n"));
    fake.responses.clear();
    run = run_cli({"--version"}, &services);
    PRK_EXPECT(run.exit_code == 0 &&
               contains(run.output, "Could not check for updates: no release has been published"));
    const std::size_t requests = fake.requested.size();
    run = run_cli({"version", "-offline"}, &services);
    PRK_EXPECT(run.exit_code == 0 && run.output.find(version_line) == 0 && !contains(run.output, "release"));
    PRK_EXPECT(fake.requested.size() == requests);
    run = run_cli({"-v"}, nullptr);
    PRK_EXPECT(run.exit_code == 0 && run.output.find('\n') == run.output.size() - 1);
    run = run_cli({"-update", "-check"}, nullptr);
    PRK_EXPECT(run.exit_code == kExitUpdateFailed && contains(run.errors, "updates need the network"));
}

}  // namespace

PrkStatus register_update_command_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"update.parses_update_and_version", &test_parses_update_and_version, nullptr},
        {"update.reports_and_checks", &test_reports_and_checks, nullptr},
        {"update.installs_verified_update", &test_installs_verified_update, nullptr},
        {"update.refuses_unverified_update", &test_refuses_unverified_update, nullptr},
        {"update.reports_lookup_failures", &test_reports_lookup_failures, nullptr},
        {"update.version_reports_release", &test_version_reports_release, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
