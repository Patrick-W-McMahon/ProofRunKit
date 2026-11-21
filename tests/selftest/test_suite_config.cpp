/**
 * @file test_suite_config.cpp
 * @brief Tests for the pure parts of `prk test` (#82): the prk.tests.json parser, `${name}` variables, paths, and
 *        the `test` command line.
 */
#include <string>
#include <vector>

#include "cpp/suite/test_command_line.hpp"
#include "cpp/suite/test_config.hpp"
#include "cpp/suite/test_suite.hpp"
#include "cpp/suite/test_variables.hpp"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

const char kFullConfig[] = R"({
  "schema_version": 1,
  "description": "sample",
  "variables": {"bin": ["${build_dir}/Debug", "${build_dir}"], "greeting": "hi"},
  "defaults": {"env": {"PRK_OFFLINE": "1"}, "timeout_ms": 1000},
  "runner": "${bin}/prk${exe}",
  "work_dir": "${build_dir}/work",
  "setup": [{"name": "fixture", "run": "a.lua", "expect_exit_code": 1, "report": "none"}],
  "tests": [
    {"name": "self.tags", "run": ["t.lua", "u.js"], "args": ["--tag", "x"], "cwd": "${bin}", "env": {"A": "b"},
     "timeout_ms": 5, "template": true, "skip_unless": "${bin}/x", "expect_output": "ok", "reject_output": "bad",
     "make_dirs": ["a", "b"]},
    {"name": "cpp", "command": ["${bin}/selftest", "--json"], "report": "json"}
  ]
})";

std::string parse_error_of(const std::string& text) {
    std::string error;
    return parse_test_config(text, error) ? std::string() : error;
}

void test_parses_full_config(PrkTestContext* context, void*) {
    std::string error;
    const std::optional<TestConfig> config = parse_test_config(kFullConfig, error);
    PRK_EXPECT(config && error.empty());
    PRK_EXPECT(config->variables.size() == 2 && config->variables[0].name == "bin" &&
               config->variables[0].is_path_list);
    PRK_EXPECT(config->variables[1].candidates == std::vector<std::string>{"hi"} && !config->variables[1].is_path_list);
    PRK_EXPECT(config->default_env.size() == 1 && config->default_timeout_ms == 1000);
    PRK_EXPECT(config->runner == "${bin}/prk${exe}" && config->work_dir == "${build_dir}/work");
    PRK_EXPECT(config->setup.size() == 1 && config->setup[0].expected_exit_code == 1);
    PRK_EXPECT(config->setup[0].is_report_read == false);
    const TestEntry& tags = config->tests.at(0);
    PRK_EXPECT(tags.name == "self.tags" && tags.scripts.size() == 2 && tags.arguments.size() == 2);
    PRK_EXPECT(tags.working_directory == "${bin}" && tags.env.size() == 1 && tags.timeout_ms == 5);
    PRK_EXPECT(tags.is_template && !tags.is_report_read.has_value() && tags.skip_unless.size() == 1);
    PRK_EXPECT(tags.expected_output == "ok" && tags.rejected_output == "bad" && tags.make_dirs.size() == 2);
    PRK_EXPECT(config->tests.at(1).command.size() == 2 && config->tests.at(1).is_report_read == true);
    const std::optional<TestConfig> minimal = parse_test_config(R"({"schema_version": 1})", error);
    PRK_EXPECT(minimal && minimal->runner == "${prk}" && minimal->default_timeout_ms == 600000);
}

void test_rejects_invalid_configs(PrkTestContext* context, void*) {
    PRK_EXPECT(parse_error_of("{").starts_with("prk.tests.json: not valid JSON"));
    PRK_EXPECT(parse_error_of("[]") == "prk.tests.json: expected an object");
    PRK_EXPECT(parse_error_of(R"({"schema_version": 2})") == "schema_version: expected 1");
    PRK_EXPECT(parse_error_of(R"({"schema_version": 1, "test": []})") == "prk.tests.json: unknown key 'test'");
    PRK_EXPECT(parse_error_of(R"({"schema_version": 1, "tests": [{"name": "a"}]})") ==
               "tests[0] (a): needs exactly one of 'run' or 'command'");
    PRK_EXPECT(parse_error_of(R"({"schema_version": 1, "tests": [{"name": "a b", "run": "x"}]})")
                   .starts_with("tests[0].name:"));
    PRK_EXPECT(
        parse_error_of(R"({"schema_version": 1, "tests": [{"name": "a", "run": []}]})").find("must not be empty") !=
        std::string::npos);
    PRK_EXPECT(parse_error_of(R"({"schema_version": 1, "tests": [{"name": "a", "run": "x", "colour": 1}]})") ==
               "tests[0]: unknown key 'colour'");
    PRK_EXPECT(parse_error_of(R"({"schema_version": 1, "tests": [{"name": "a", "run": "x", "expect_exit_code": 256}]})")
                   .find("expect_exit_code") != std::string::npos);
    PRK_EXPECT(parse_error_of(R"({"schema_version": 1, "tests": [{"name": "a", "run": "x", "env": {"A": 1}}]})")
                   .find("env.A") != std::string::npos);
    PRK_EXPECT(parse_error_of(R"({"schema_version": 1, "setup": [{"name": "a", "run": "x"}],
                                  "tests": [{"name": "a", "run": "y"}]})") == "tests[0]: duplicate entry name 'a'");
    PRK_EXPECT(parse_error_of(R"({"schema_version": 1, "runner": 3})") == "runner: expected a non-empty string");
    PRK_EXPECT(is_valid_test_entry_name("self.cli-test_2") && !is_valid_test_entry_name(""));
    PRK_EXPECT(!is_valid_test_entry_name("a/b") && !is_valid_test_entry_name(std::string(101, 'a')));
}

void test_expands_variables(PrkTestContext* context, void*) {
    const TestVariables variables{{"a", "1"}, {"build_dir", "/b"}};
    std::string missing;
    PRK_EXPECT(expand_test_variables("x${a}y${build_dir}", variables, missing) == "x1y/b");
    PRK_EXPECT(expand_test_variables("$${a} costs $5", variables, missing) == "${a} costs $5");
    PRK_EXPECT(!expand_test_variables("${nope}", variables, missing) && missing == "nope");
    PRK_EXPECT(!expand_test_variables("${a", variables, missing) && missing == "${a");
    PRK_EXPECT(expand_test_variables("", variables, missing) == "");
}

void test_resolves_variables(PrkTestContext* context, void*) {
    TestConfig config;
    config.variables = {{"z_bin", {"${build_dir}/Debug", "${build_dir}"}, true},
                        {"a_runner", {"${z_bin}/prk"}, false},
                        {"loop1", {"${loop2}"}, false},
                        {"loop2", {"${loop1}"}, false},
                        {"fallback", {"${no_such}/x", "${prk}"}, true},
                        {"over", {"config"}, false}};
    const TestVariables builtins{{"build_dir", "/b"}, {"prk", "/p/prk"}};
    const TestVariables overrides{{"over", "cli"}};
    const auto exists = [](const std::string& path) { return path == "/b" || path == "/p/prk"; };
    const TestVariables resolved = resolve_test_variables(config, builtins, overrides, exists);
    PRK_EXPECT(resolved.at("z_bin") == "/b" && resolved.at("a_runner") == "/b/prk");
    PRK_EXPECT(resolved.count("loop1") == 0 && resolved.count("loop2") == 0);
    PRK_EXPECT(resolved.at("fallback") == "/p/prk" && resolved.at("over") == "cli");
    const TestVariables without_build = resolve_test_variables(config, {{"prk", "/p/prk"}}, {}, exists);
    PRK_EXPECT(without_build.count("z_bin") == 0 && without_build.count("a_runner") == 0);
}

void test_joins_suite_paths(PrkTestContext* context, void*) {
    PRK_EXPECT(join_suite_path("/repo", "tests/a.lua", '/') == "/repo/tests/a.lua");
    PRK_EXPECT(join_suite_path("C:\\repo", "a.lua", '\\') == "C:\\repo\\a.lua");
    PRK_EXPECT(join_suite_path("/repo/", "a", '/') == "/repo/a" && join_suite_path("/repo", ".", '/') == "/repo");
    PRK_EXPECT(join_suite_path("/repo", "/abs", '/') == "/abs" && join_suite_path("/repo", "D:\\x", '/') == "D:\\x");
    PRK_EXPECT(join_suite_path("", "rel", '/') == "rel");
}

std::vector<std::string_view> make_arguments(const std::vector<std::string_view>& list) {
    return list;
}

void test_parses_test_command(PrkTestContext* context, void*) {
    const auto arguments = make_arguments({"test", "--config", "c.json", "--build-dir", "build", "--var", "a=b=c",
                                           "--filter", "self.", "--json", "--junit-file", "r.xml"});
    const CommandLineParseResult parsed = parse_command_line(arguments);
    PRK_EXPECT(parsed.error_message.empty() && parsed.options.command == CliCommand::Test);
    PRK_EXPECT(parsed.options.suite_config_path == "c.json" && parsed.options.suite_build_dir == "build");
    PRK_EXPECT(parsed.options.suite_variables == std::vector<std::string>{"a=b=c"});
    PRK_EXPECT(parsed.options.name_filter == "self." && parsed.options.report_format == ReportFormat::Json);
    PRK_EXPECT(parsed.options.junit_file == "r.xml");
    PRK_EXPECT(parse_suite_command(make_arguments({"--list"})).options.list_tests);
    PRK_EXPECT(parse_suite_command(make_arguments({"--var", "=x"})).error_message.starts_with("--var needs"));
    PRK_EXPECT(parse_suite_command(make_arguments({"--build-dir"})).error_message == "--build-dir needs a value");
    PRK_EXPECT(parse_suite_command(make_arguments({"x.lua"})).error_message.starts_with("unknown test option"));
    PRK_EXPECT(!parse_suite_command(make_arguments({"--list", "--json-file", "a"})).error_message.empty());
    PRK_EXPECT(parse_suite_command(make_arguments({"--reporter", "junit"})).options.report_format ==
               ReportFormat::Junit);
    const CommandLineParseResult help = parse_command_line(make_arguments({"help", "test"}));
    PRK_EXPECT(help.options.show_help && help.options.help_topic == "test");
    PRK_EXPECT(parse_suite_command(make_arguments({"-h"})).options.help_topic == "test");
    const std::string text = format_suite_help_text("prk");
    PRK_EXPECT(text.starts_with("Usage: prk test [options]") && text.find("--build-dir DIR") != std::string::npos);
    PRK_EXPECT(format_command_help_text("prk", "test") == text);
    PRK_EXPECT(format_usage_text("prk").find("  test ") != std::string::npos);
}

}  // namespace

PrkStatus register_suite_config_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"suite.parses_full_config", &test_parses_full_config, nullptr},
        {"suite.rejects_invalid_configs", &test_rejects_invalid_configs, nullptr},
        {"suite.expands_variables", &test_expands_variables, nullptr},
        {"suite.resolves_variables", &test_resolves_variables, nullptr},
        {"suite.joins_paths", &test_joins_suite_paths, nullptr},
        {"suite.parses_test_command", &test_parses_test_command, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
