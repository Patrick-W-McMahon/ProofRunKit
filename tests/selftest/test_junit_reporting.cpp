/**
 * @file test_junit_reporting.cpp
 * @brief Tests for the JUnit XML report, the reporter fan-out, and report files from the command line (#21).
 */
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cpp/command_line_app.hpp"
#include "cpp/junit_reporter.hpp"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

using RegistryPointer = std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)>;

void body_that_passes(PrkTestContext*, void*) {}

void body_that_fails(PrkTestContext* context, void*) {
    prk_fail_test(context, "expected failure");
}

void skip_body(PrkTestContext* context, void*) {
    prk_skip_test(context, "not on this machine");
}

/** @brief Registry with one passing, one failing, and one skipped test. */
RegistryPointer make_fixture_registry() {
    RegistryPointer registry(prk_create_registry(), &prk_destroy_registry);
    const PrkTestCase cases[] = {{"suite.pass", &body_that_passes, nullptr},
                                 {"suite.inner.fail", &body_that_fails, nullptr},
                                 {"lonely", &skip_body, nullptr}};
    for (const PrkTestCase& test_case : cases) {
        (void)prk_register_test_case(registry.get(), &test_case);
    }
    return registry;
}

void test_xml_escaping(PrkTestContext* context, void*) {
    PRK_EXPECT(escape_xml_text("plain") == "plain");
    PRK_EXPECT(escape_xml_text("a<b>&\"c'") == "a&lt;b&gt;&amp;&quot;c&apos;");
    PRK_EXPECT(escape_xml_text("l1\nl2\tt\r") == "l1&#10;l2&#9;t&#13;");
    PRK_EXPECT(escape_xml_text(std::string("\x01x\x1f", 3)) == "?x?");
    PRK_EXPECT(escape_xml_text("caf\xc3\xa9") == "caf\xc3\xa9");
}

void test_splits_test_names(PrkTestContext* context, void*) {
    PRK_EXPECT(split_junit_test_name("a.b.c") == std::make_pair(std::string("a.b"), std::string("c")));
    PRK_EXPECT(split_junit_test_name("single") == std::make_pair(std::string("proofrunkit"), std::string("single")));
    PRK_EXPECT(split_junit_test_name(".hidden").first == "proofrunkit");
    PRK_EXPECT(split_junit_test_name("trailing.").second == "trailing.");
}

void test_junit_document(PrkTestContext* context, void*) {
    const std::vector<RecordedTestResult> results = {
        {"suite.pass", PRK_OUTCOME_PASSED, "", 1},
        {"suite.fail", PRK_OUTCOME_FAILED, "x.lua:3: bad <value>\ntrace", 2},
        {"skipped_one", PRK_OUTCOME_SKIPPED, "later", 3}};
    const std::string xml = format_junit_report(results, {0.5, 0.25}, PrkRunSummary{3, 1, 1, 1});
    PRK_EXPECT(
        xml.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites name=\"proofrunkit\" "
                        "tests=\"3\" failures=\"1\" errors=\"0\" skipped=\"1\" time=\"0.750\">\n"));
    PRK_EXPECT(xml.find("<testcase classname=\"suite\" name=\"pass\" time=\"0.500\"/>") != std::string::npos);
    PRK_EXPECT(
        xml.find("<testcase classname=\"suite\" name=\"fail\" time=\"0.250\">\n      <failure message=\""
                 "x.lua:3: bad &lt;value&gt;\" type=\"failure\">x.lua:3: bad &lt;value&gt;&#10;trace</failure>") !=
        std::string::npos);
    PRK_EXPECT(xml.find("<testcase classname=\"proofrunkit\" name=\"skipped_one\" time=\"0.000\">\n"
                        "      <skipped message=\"later\"/>") != std::string::npos);
    PRK_EXPECT(xml.find("<property name=\"proofrunkit.version\"") != std::string::npos);
    PRK_EXPECT(xml.ends_with("  </testsuite>\n</testsuites>\n"));
}

void test_junit_reporter_times_tests_and_fans_out(PrkTestContext* context, void*) {
    const RegistryPointer registry = make_fixture_registry();
    double now = 10.0;
    std::ostringstream junit_text;
    std::ostringstream console_text;
    JUnitReporter junit(junit_text, [&now] { return now += 0.125; });
    ConsoleReporter console(console_text);
    ReporterFanOut fan_out;
    fan_out.add_reporter(junit.make_c_reporter());
    fan_out.add_reporter(console.make_c_reporter());
    const PrkReporter reporter = fan_out.make_c_reporter();
    PrkRunSummary summary{};
    PRK_EXPECT(prk_run_test_cases(registry.get(), nullptr, &reporter, &summary) == PRK_STATUS_OK);
    PRK_EXPECT(summary.total == 3 && summary.failed == 1 && summary.skipped == 1);
    PRK_EXPECT(junit_text.str().find("name=\"pass\" time=\"0.125\"") != std::string::npos);
    PRK_EXPECT(junit_text.str().find("time=\"0.375\">") != std::string::npos);  // the suite total
    PRK_EXPECT(console_text.str().ends_with("3 tests: 1 passed, 1 failed, 1 skipped\n"));
}

/** @brief Runs the command line against the fixture registry, capturing report files in memory. */
struct ReportRun {
    int exit_code = 0;
    std::string output;
    std::string errors;
    std::map<std::string, std::string> files;
};

ReportRun run_with_report_files(std::vector<std::string_view> arguments) {
    const RegistryPointer registry = make_fixture_registry();
    ReportRun run;
    std::ostringstream output;
    std::ostringstream errors;
    const ReportFileWriter write_file = [&run](const std::string& path, const std::string& text) {
        if (path.starts_with("readonly/")) {
            return std::string("cannot open file for writing");
        }
        run.files[path] = text;
        return std::string();
    };
    const auto no_scripts = [](const std::string&) { return ScriptFileContent{false, {}, "cannot open file"}; };
    const CommandLineEnvironment environment{
        *registry, PrkClock{nullptr, nullptr}, no_scripts, ScriptHostServices{}, write_file, output, errors, nullptr};
    run.exit_code = run_command_line("prk", arguments, environment);
    run.output = output.str();
    run.errors = errors.str();
    return run;
}

void test_command_line_writes_report_files(PrkTestContext* context, void*) {
    ReportRun run = run_with_report_files({"run", "--json-file", "out/r.json", "--junit-file", "out/r.xml"});
    PRK_EXPECT(run.exit_code == kExitTestFailed);
    PRK_EXPECT(run.output.find("[ FAIL ] suite.inner.fail") != std::string::npos);
    PRK_EXPECT(run.files.count("out/r.json") == 1 && run.files.count("out/r.xml") == 1);
    PRK_EXPECT(run.files["out/r.json"].starts_with("{\n  \"schema_version\": 1,"));
    PRK_EXPECT(run.files["out/r.xml"].find("classname=\"suite.inner\" name=\"fail\"") != std::string::npos);
    const ReportRun junit_stdout = run_with_report_files({"run", "--reporter", "junit", "--filter", "suite.pass"});
    PRK_EXPECT(junit_stdout.exit_code == kExitAllPassed && junit_stdout.output.starts_with("<?xml"));
}

void test_report_exit_codes(PrkTestContext* context, void*) {
    const ReportRun unwritable = run_with_report_files({"run", "--filter", "suite.pass", "--junit-file", "readonly/x"});
    PRK_EXPECT(unwritable.exit_code == kExitReportError);
    PRK_EXPECT(unwritable.errors == "prk: cannot write report 'readonly/x': cannot open file for writing\n");
    const ReportRun failing_and_unwritable = run_with_report_files({"run", "--json-file", "readonly/x"});
    PRK_EXPECT(failing_and_unwritable.exit_code == kExitReportError);  // tool errors win over test failures
    const ReportRun listing = run_with_report_files({"list", "--json-file", "out/r.json"});
    PRK_EXPECT(listing.exit_code == kExitUsageError && listing.files.empty());
    PRK_EXPECT(run_with_report_files({"run", "--junit-file"}).exit_code == kExitUsageError);
    PRK_EXPECT(run_with_report_files({"run", "--reporter", "xml"}).exit_code == kExitUsageError);
}

}  // namespace

PrkStatus register_junit_reporting_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"cli.reporting.xml_escaping", &test_xml_escaping, nullptr},
        {"cli.reporting.junit_splits_test_names", &test_splits_test_names, nullptr},
        {"cli.reporting.junit_document", &test_junit_document, nullptr},
        {"cli.reporting.junit_reporter_times_tests_and_fans_out", &test_junit_reporter_times_tests_and_fans_out,
         nullptr},
        {"cli.reporting.command_line_writes_report_files", &test_command_line_writes_report_files, nullptr},
        {"cli.reporting.report_exit_codes", &test_report_exit_codes, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
