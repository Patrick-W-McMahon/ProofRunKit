/**
 * @file test_command_line.cpp
 * @brief Tests for prk::parse_command_line, the help text, prk::select_program_name, and prk::run_command_line.
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

void test_parses_run_subcommand(PrkTestContext* context, void*) {
    const CommandLineParseResult parsed = parse_arguments({"run", "--filter", "core.", "--json", "a.lua", "b.js"});
    PRK_EXPECT(parsed.error_message.empty());
    PRK_EXPECT(parsed.options.command == CliCommand::Run);
    PRK_EXPECT(!parsed.options.list_tests && !parsed.options.show_help && !parsed.options.show_version);
    PRK_EXPECT(parsed.options.name_filter == "core.");
    PRK_EXPECT(parsed.options.report_format == ReportFormat::Json);
    PRK_EXPECT(parsed.options.script_paths == std::vector<std::string>({"a.lua", "b.js"}));
    const CommandLineParseResult reporter = parse_arguments({"run", "--reporter", "json"});
    PRK_EXPECT(reporter.error_message.empty() && reporter.options.report_format == ReportFormat::Json);
    const CommandLineParseResult console = parse_arguments({"run", "--json", "--reporter", "console"});
    PRK_EXPECT(console.options.report_format == ReportFormat::Console);
}

void test_parses_list_version_and_help_subcommands(PrkTestContext* context, void*) {
    const CommandLineParseResult listing = parse_arguments({"list", "--filter", "kit."});
    PRK_EXPECT(listing.error_message.empty() && listing.options.command == CliCommand::List);
    PRK_EXPECT(listing.options.list_tests && listing.options.name_filter == "kit.");
    const CommandLineParseResult version = parse_arguments({"version"});
    PRK_EXPECT(version.error_message.empty() && version.options.show_version);
    const CommandLineParseResult help = parse_arguments({"help"});
    PRK_EXPECT(help.options.show_help && help.options.help_topic.empty());
    const CommandLineParseResult help_run = parse_arguments({"help", "run"});
    PRK_EXPECT(help_run.options.show_help && help_run.options.help_topic == "run");
    const CommandLineParseResult run_help = parse_arguments({"run", "--help"});
    PRK_EXPECT(run_help.options.show_help && run_help.options.help_topic == "run");
    const CommandLineParseResult list_help = parse_arguments({"list", "-h"});
    PRK_EXPECT(list_help.options.show_help && list_help.options.help_topic == "list");
}

void test_reports_subcommand_usage_errors(PrkTestContext* context, void*) {
    PRK_EXPECT(parse_arguments({"help", "bogus"}).error_message == "unknown command 'bogus' (see help)");
    PRK_EXPECT(parse_arguments({"help", "run", "list"}).error_message == "help takes at most one command name");
    PRK_EXPECT(parse_arguments({"version", "x"}).error_message.find("version takes no arguments") == 0);
    PRK_EXPECT(parse_arguments({"run", "--list"}).error_message == "unknown option '--list'");
    PRK_EXPECT(parse_arguments({"run", "--version"}).error_message == "unknown option '--version'");
    PRK_EXPECT(parse_arguments({"run", "--reporter"}).error_message == "--reporter needs a value");
    PRK_EXPECT(parse_arguments({"list", "--reporter", "xml"}).error_message.find("unknown --reporter 'xml'") == 0);
}

void test_flag_form_accepts_json_and_reporter(PrkTestContext* context, void*) {
    const CommandLineParseResult json = parse_arguments({"--json", "suite.lua"});
    PRK_EXPECT(json.error_message.empty() && json.options.command == CliCommand::Default);
    PRK_EXPECT(json.options.report_format == ReportFormat::Json && json.options.script_paths.size() == 1);
    const CommandLineParseResult reporter = parse_arguments({"--reporter", "json", "--list"});
    PRK_EXPECT(reporter.options.report_format == ReportFormat::Json && reporter.options.list_tests);
    const CommandLineParseResult script_named_run = parse_arguments({"./run"});
    PRK_EXPECT(script_named_run.options.command == CliCommand::Default);
    PRK_EXPECT(script_named_run.options.script_paths == std::vector<std::string>({"./run"}));
}

void test_command_help_text(PrkTestContext* context, void*) {
    const std::string run_help = format_command_help_text("prk", "run");
    PRK_EXPECT(run_help.find("Usage: prk run [options] [test scripts...]") == 0);
    PRK_EXPECT(run_help.find("--reporter FORMAT") != std::string::npos);
    PRK_EXPECT(run_help.find("--json") != std::string::npos);
    PRK_EXPECT(format_command_help_text("prk", "list").find("Usage: prk list") == 0);
    PRK_EXPECT(format_command_help_text("prk", "version").find("Usage: prk version") == 0);
    PRK_EXPECT(format_command_help_text("prk", "help").find("Usage: prk help [command]") == 0);
    PRK_EXPECT(format_command_help_text("prk", "bogus").empty());
    const std::string usage = format_usage_text("prk");
    PRK_EXPECT(usage.find("prk <command> [arguments]") != std::string::npos);
    PRK_EXPECT(usage.find("  run [scripts...]") != std::string::npos);
}

void test_selects_program_name_from_invoked_path(PrkTestContext* context, void*) {
    PRK_EXPECT(select_program_name("prk") == "prk");
    PRK_EXPECT(select_program_name("prk.exe") == "prk");
    PRK_EXPECT(select_program_name("C:\\Program Files\\ProofRunKit\\PRK.EXE") == "prk");
    PRK_EXPECT(select_program_name("/usr/local/bin/prk") == "prk");
    PRK_EXPECT(select_program_name("C:\\Program Files\\ProofRunKit\\proofrunkit.exe") == "proofrunkit");
    PRK_EXPECT(select_program_name("") == "proofrunkit");
    PRK_EXPECT(select_program_name("prkx.exe") == "proofrunkit");
    PRK_EXPECT(select_program_name("C:\\prk\\tool.exe") == "proofrunkit");
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
    const auto read_no_files = [](const std::string&) { return ScriptFileContent{false, {}, "cannot open file"}; };
    const CommandLineEnvironment environment{*registry, PrkClock{nullptr, nullptr}, read_no_files, output, errors};
    const int exit_code = run_command_line("prk", arguments, environment);
    return CapturedRun{exit_code, output.str(), errors.str()};
}

void test_app_exit_codes(PrkTestContext* context, void*) {
    PRK_EXPECT(run_with_sample_registry({}).exit_code == kExitTestFailed);
    PRK_EXPECT(run_with_sample_registry({"--filter", "sample."}).exit_code == kExitAllPassed);
    PRK_EXPECT(run_with_sample_registry({"--nope"}).exit_code == kExitUsageError);
    PRK_EXPECT(run_with_sample_registry({"suite.lua"}).exit_code == kExitScriptError);
    PRK_EXPECT(run_with_sample_registry({"suite.js"}).exit_code == kExitNotImplemented);
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

void test_app_runs_subcommands(PrkTestContext* context, void*) {
    PRK_EXPECT(run_with_sample_registry({"run"}).exit_code == kExitTestFailed);
    const CapturedRun passed = run_with_sample_registry({"run", "--filter", "sample.", "--json"});
    PRK_EXPECT(passed.exit_code == kExitAllPassed && passed.output.find("\"passed\": 1") != std::string::npos);
    const CapturedRun listing = run_with_sample_registry({"list", "--filter", "other"});
    PRK_EXPECT(listing.exit_code == kExitAllPassed && listing.output == "other.fail\n");
    const CapturedRun version = run_with_sample_registry({"version"});
    PRK_EXPECT(version.output.find(std::string("ProofRunKit ") + prk_get_version_string()) == 0);
    PRK_EXPECT(run_with_sample_registry({"run", "suite.lua"}).exit_code == kExitScriptError);
}

void test_app_prints_command_help_and_errors(PrkTestContext* context, void*) {
    const CapturedRun help_run = run_with_sample_registry({"help", "run"});
    PRK_EXPECT(help_run.exit_code == kExitAllPassed && help_run.output.find("Usage: prk run") == 0);
    const CapturedRun list_help = run_with_sample_registry({"list", "--help"});
    PRK_EXPECT(list_help.output.find("Usage: prk list") == 0);
    const CapturedRun general = run_with_sample_registry({"help"});
    PRK_EXPECT(general.output.find("Usage: prk [options]") == 0);
    const CapturedRun bogus = run_with_sample_registry({"help", "bogus"});
    PRK_EXPECT(bogus.exit_code == kExitUsageError && bogus.errors.find("prk: unknown command 'bogus'") == 0);
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
        {"cli.parse.run_subcommand", &test_parses_run_subcommand, nullptr},
        {"cli.parse.list_version_and_help_subcommands", &test_parses_list_version_and_help_subcommands, nullptr},
        {"cli.parse.reports_subcommand_usage_errors", &test_reports_subcommand_usage_errors, nullptr},
        {"cli.parse.flag_form_accepts_json_and_reporter", &test_flag_form_accepts_json_and_reporter, nullptr},
        {"cli.help.command_help_text", &test_command_help_text, nullptr},
        {"cli.name.selects_program_name_from_invoked_path", &test_selects_program_name_from_invoked_path, nullptr},
        {"cli.app.exit_codes", &test_app_exit_codes, nullptr},
        {"cli.app.help_version_and_list", &test_app_help_version_and_list, nullptr},
        {"cli.app.writes_errors_and_json", &test_app_writes_errors_and_json, nullptr},
        {"cli.app.runs_subcommands", &test_app_runs_subcommands, nullptr},
        {"cli.app.prints_command_help_and_errors", &test_app_prints_command_help_and_errors, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
