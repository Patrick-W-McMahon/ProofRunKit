/**
 * @file junit_reporter.cpp
 * @brief Implementation of the JUnit XML report and the reporter fan-out (#21).
 */
#include "junit_reporter.hpp"

#include <chrono>
#include <cstdio>
#include <utility>

namespace prk {

namespace {

/** @brief Formats seconds with millisecond precision, e.g. "0.042". */
std::string format_seconds(double seconds) {
    char text[32];
    (void)std::snprintf(text, sizeof text, "%.3f", seconds < 0.0 ? 0.0 : seconds);
    return text;
}

/** @brief Returns the first line of @p message (JUnit tools show the message attribute on one line). */
std::string_view find_first_line(std::string_view message) {
    return message.substr(0, message.find('\n'));
}

/** @brief Appends one <testcase> element. */
void append_junit_test_case(std::string& xml, const RecordedTestResult& result, double seconds) {
    const auto [classname, name] = split_junit_test_name(result.name);
    xml += "    <testcase classname=\"" + escape_xml_text(classname) + "\" name=\"" + escape_xml_text(name) +
           "\" time=\"" + format_seconds(seconds) + "\"";
    if (result.outcome == PRK_OUTCOME_PASSED) {
        xml += "/>\n";
        return;
    }
    xml += ">\n";
    if (result.outcome == PRK_OUTCOME_FAILED) {
        xml += "      <failure message=\"" + escape_xml_text(find_first_line(result.message)) + "\" type=\"failure\">" +
               escape_xml_text(result.message) + "</failure>\n";
    } else {
        xml += "      <skipped message=\"" + escape_xml_text(result.message) + "\"/>\n";
    }
    xml += "    </testcase>\n";
}

}  // namespace

std::string escape_xml_text(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            case '\t':
                escaped += "&#9;";
                break;
            case '\n':
                escaped += "&#10;";
                break;
            case '\r':
                escaped += "&#13;";
                break;
            default:
                escaped += static_cast<unsigned char>(character) < 0x20 ? '?' : character;
                break;
        }
    }
    return escaped;
}

std::pair<std::string, std::string> split_junit_test_name(std::string_view test_name) {
    const std::size_t dot = test_name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == test_name.size()) {
        return {"proofrunkit", std::string(test_name)};
    }
    return {std::string(test_name.substr(0, dot)), std::string(test_name.substr(dot + 1))};
}

std::string format_junit_report(const std::vector<RecordedTestResult>& results,
                                const std::vector<double>& elapsed_seconds, const PrkRunSummary& summary) {
    double total_seconds = 0.0;
    for (const double seconds : elapsed_seconds) {
        total_seconds += seconds;
    }
    const std::string counts = "tests=\"" + std::to_string(summary.total) + "\" failures=\"" +
                               std::to_string(summary.failed) + "\" errors=\"0\" skipped=\"" +
                               std::to_string(summary.skipped) + "\" time=\"" + format_seconds(total_seconds) + "\"";
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<testsuites name=\"proofrunkit\" " + counts + ">\n";
    xml += "  <testsuite name=\"proofrunkit\" " + counts + ">\n";
    xml += "    <properties>\n      <property name=\"proofrunkit.version\" value=\"" +
           escape_xml_text(prk_get_version_string()) + "\"/>\n    </properties>\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        append_junit_test_case(xml, results[index], index < elapsed_seconds.size() ? elapsed_seconds[index] : 0.0);
    }
    xml += "  </testsuite>\n</testsuites>\n";
    return xml;
}

SecondsClock make_steady_seconds_clock() {
    return [] {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration<double>(now).count();
    };
}

PrkReporter JUnitReporter::make_c_reporter() {
    return PrkReporter{&JUnitReporter::start_run, &JUnitReporter::collect_test_result,
                       &JUnitReporter::write_junit_document, this};
}

void JUnitReporter::start_run(void* reporter_state, std::size_t /*selected_test_count*/) {
    auto* const self = static_cast<JUnitReporter*>(reporter_state);
    self->last_finish_seconds_ = self->read_clock();
}

void JUnitReporter::collect_test_result(void* reporter_state, const PrkTestResult* result) {
    auto* const self = static_cast<JUnitReporter*>(reporter_state);
    const double now = self->read_clock();
    self->results_.push_back(record_test_result(*result));
    self->elapsed_seconds_.push_back(now - self->last_finish_seconds_);
    self->last_finish_seconds_ = now;
}

void JUnitReporter::write_junit_document(void* reporter_state, const PrkRunSummary* summary) {
    auto* const self = static_cast<JUnitReporter*>(reporter_state);
    self->output_ << format_junit_report(self->results_, self->elapsed_seconds_, *summary);
}

PrkReporter ReporterFanOut::make_c_reporter() {
    return PrkReporter{&ReporterFanOut::forward_run_started, &ReporterFanOut::forward_test_finished,
                       &ReporterFanOut::forward_run_finished, this};
}

void ReporterFanOut::forward_run_started(void* reporter_state, std::size_t selected_test_count) {
    for (const PrkReporter& reporter : static_cast<ReporterFanOut*>(reporter_state)->reporters_) {
        if (reporter.report_run_started != nullptr) {
            reporter.report_run_started(reporter.reporter_state, selected_test_count);
        }
    }
}

void ReporterFanOut::forward_test_finished(void* reporter_state, const PrkTestResult* result) {
    for (const PrkReporter& reporter : static_cast<ReporterFanOut*>(reporter_state)->reporters_) {
        if (reporter.report_test_finished != nullptr) {
            reporter.report_test_finished(reporter.reporter_state, result);
        }
    }
}

void ReporterFanOut::forward_run_finished(void* reporter_state, const PrkRunSummary* summary) {
    for (const PrkReporter& reporter : static_cast<ReporterFanOut*>(reporter_state)->reporters_) {
        if (reporter.report_run_finished != nullptr) {
            reporter.report_run_finished(reporter.reporter_state, summary);
        }
    }
}

}  // namespace prk
