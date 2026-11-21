/**
 * @file test_command_line.cpp
 * @brief Tests for prk::parse_command_line, prk::format_usage_text, and prk::run_command_line.
 */
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cpp/command_line.hpp"
#include "cpp/command_line_app.hpp"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

CommandLineParseResult parse_arguments(std::vector<std::string_view> arguments) {
    return parse_command_line(arguments);
}

void test_defaults_with_no_arguments(PrkTestContext* context, void*) {
    const CommandLineParseResult parsed = parse_arguments({});
    PRK_EXPECT(parsed.error_message.empty());
    PRK_EXPECT(!parsed.options.show_help && !parsed.options.show_version && !parsed.options.list_tests);
    PRK_EXPECT(parsed.options.report_format == ReportFormat::Console);
    PRK_EXPECT(parsed.options.name_filter.empty() && parsed.options.script_paths.empty());
}

void test_parses_flags_values_and_scripts(PrkTestContext* context, void*) {
    const CommandLineParseResult parsed =
        parse_arguments({"--list", "--filter", "core.", "--format", "json", "tests/a.lua", "-h", "--version"});
    PRK_EXPECT(parsed.error_message.empty());
    PRK_EXPECT(parsed.options.list_tests && parsed.options.show_help && parsed.options.show_version);
    PRK_EXPECT(parsed.options.name_filter == "core.");
    PRK_EXPECT(parsed.options.report_format == ReportFormat::Json);
    PRK_EXPECT(parsed.options.script_paths.size() == 1 && parsed.options.script_paths[0] == "tests/a.lua");
}

void test_reports_usage_errors(PrkTestContext* context, void*) {
    PRK_EXPECT(parse_arguments({"--filter"}).error_message == "--filter needs a value");
    PRK_EXPECT(parse_arguments({"--format", "xml"}).error_message.find("unknown --format 'xml'") == 0);
    PRK_EXPECT(parse_arguments({"--bogus"}).error_message == "unknown option '--bogus'");
}

void test_usage_text_names_program_and_options(PrkTestContext* context, void*) {
    const std::string usage = format_usage_text("proofrunkit");
    PRK_EXPECT(usage.find("Usage: proofrunkit [options]") == 0);
    PRK_EXPECT(usage.find("--filter TEXT") != std::string::npos);
    PRK_EXPECT(usage.find("--format FORMAT") != std::string::npos);
}

void body_that_passes(PrkTestContext*, void*) {}
void body_that_fails(PrkTestContext* context, void*) {
    prk_fail_test(context, "expected failure");
}

/** @brief Result of driving run_command_line with captured streams. */
struct CapturedRun {
    int exit_code;
    std::string output;
    std::string errors;
};

CapturedRun run_with_sample_registry(std::vector<std::string_view> arguments) {
    const std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)> registry(prk_create_registry(),
                                                                                 &prk_destroy_registry);
    const PrkTestCase pass_case{"sample.pass", &body_that_passes, nullptr};
    const PrkTestCase fail_case{"other.fail", &body_that_fails, nullptr};
    (void)prk_register_test_case(registry.get(), &pass_case);
    (void)prk_register_test_case(registry.get(), &fail_case);
    std::ostringstream output;
    std::ostringstream errors;
    const int exit_code = run_command_line("prk", arguments, *registry, PrkClock{nullptr, nullptr}, output, errors);
    return CapturedRun{exit_code, output.str(), errors.str()};
}

void test_app_exit_codes(PrkTestContext* context, void*) {
    PRK_EXPECT(run_with_sample_registry({}).exit_code == kExitTestFailed);
    PRK_EXPECT(run_with_sample_registry({"--filter", "sample."}).exit_code == kExitAllPassed);
    PRK_EXPECT(run_with_sample_registry({"--nope"}).exit_code == kExitUsageError);
    PRK_EXPECT(run_with_sample_registry({"suite.lua"}).exit_code == kExitNotImplemented);
}

void test_app_help_version_and_list(PrkTestContext* context, void*) {
    const CapturedRun help = run_with_sample_registry({"--help"});
    PRK_EXPECT(help.exit_code == kExitAllPassed && help.output.find("Usage: prk") == 0);
    const CapturedRun version = run_with_sample_registry({"--version"});
    PRK_EXPECT(version.output.find(std::string("ProofRunKit ") + prk_get_version_string()) == 0);
    PRK_EXPECT(version.output.find(prk_get_cycle_counter_implementation_name()) != std::string::npos);
    const CapturedRun listing = run_with_sample_registry({"--list"});
    PRK_EXPECT(listing.output == "sample.pass\nother.fail\n");
    const CapturedRun filtered = run_with_sample_registry({"--list", "--filter", "other"});
    PRK_EXPECT(filtered.output == "other.fail\n");
}

void test_app_writes_errors_and_json(PrkTestContext* context, void*) {
    const CapturedRun usage = run_with_sample_registry({"--format", "xml"});
    PRK_EXPECT(usage.output.empty() && usage.errors.find("prk: unknown --format") == 0);
    const CapturedRun scripts = run_with_sample_registry({"suite.js"});
    PRK_EXPECT(scripts.errors.find("not implemented yet") != std::string::npos);
    const CapturedRun json = run_with_sample_registry({"--format", "json", "--filter", "sample."});
    PRK_EXPECT(json.output.find("\"passed\": 1") != std::string::npos);
}

}  // namespace

PrkStatus register_command_line_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"cli.parse.defaults_with_no_arguments", &test_defaults_with_no_arguments, nullptr},
        {"cli.parse.parses_flags_values_and_scripts", &test_parses_flags_values_and_scripts, nullptr},
        {"cli.parse.reports_usage_errors", &test_reports_usage_errors, nullptr},
        {"cli.parse.usage_text_names_program_and_options", &test_usage_text_names_program_and_options, nullptr},
        {"cli.app.exit_codes", &test_app_exit_codes, nullptr},
        {"cli.app.help_version_and_list", &test_app_help_version_and_list, nullptr},
        {"cli.app.writes_errors_and_json", &test_app_writes_errors_and_json, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
