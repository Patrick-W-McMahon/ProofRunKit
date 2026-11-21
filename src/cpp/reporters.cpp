/**
 * @file reporters.cpp
 * @brief Implementation of the console and JSON reporters.
 */
#include "reporters.hpp"

#include <cstdio>

namespace prk {

namespace {

/** @brief Returns the fixed-width console tag for an outcome. */
std::string_view get_console_outcome_tag(PrkOutcome outcome) {
    switch (outcome) {
        case PRK_OUTCOME_PASSED:
            return "[ PASS ]";
        case PRK_OUTCOME_FAILED:
            return "[ FAIL ]";
        case PRK_OUTCOME_SKIPPED:
            return "[ SKIP ]";
        default:
            return "[ ???? ]";
    }
}

}  // namespace

RecordedTestResult record_test_result(const PrkTestResult& result) {
    return RecordedTestResult{result.name == nullptr ? "" : result.name, result.outcome,
                              result.message == nullptr ? "" : result.message, result.elapsed_ticks};
}

std::string format_test_result_line(const RecordedTestResult& result) {
    std::string line(get_console_outcome_tag(result.outcome));
    line += ' ';
    line += result.name;
    line += " (" + std::to_string(result.elapsed_ticks) + " ticks)";
    if (!result.message.empty()) {
        line += ": " + result.message;
    }
    return line;
}

std::string format_run_summary_line(const PrkRunSummary& summary) {
    return std::to_string(summary.total) + (summary.total == 1 ? " test: " : " tests: ") +
           std::to_string(summary.passed) + " passed, " + std::to_string(summary.failed) + " failed, " +
           std::to_string(summary.skipped) + " skipped";
}

std::string escape_json_string(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(character) < 0x20) {
                    char code[8];
                    std::snprintf(code, sizeof(code), "\\u%04x", static_cast<unsigned>(character));
                    escaped += code;
                } else {
                    escaped += character;
                }
        }
    }
    return escaped;
}

std::string format_json_report(const std::vector<RecordedTestResult>& results, const PrkRunSummary& summary) {
    std::string json = "{\n  \"tool\": \"proofrunkit\",\n  \"version\": \"";
    json += escape_json_string(prk_get_version_string());
    json += "\",\n  \"summary\": {\"total\": " + std::to_string(summary.total) +
            ", \"passed\": " + std::to_string(summary.passed) + ", \"failed\": " + std::to_string(summary.failed) +
            ", \"skipped\": " + std::to_string(summary.skipped) + "},\n  \"tests\": [";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const RecordedTestResult& result = results[index];
        json += index == 0 ? "\n" : ",\n";
        json += "    {\"name\": \"" + escape_json_string(result.name) + "\", \"outcome\": \"" +
                prk_get_outcome_name(result.outcome) +
                "\", \"elapsed_ticks\": " + std::to_string(result.elapsed_ticks) + ", \"message\": \"" +
                escape_json_string(result.message) + "\"}";
    }
    json += results.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return json;
}

PrkReporter ConsoleReporter::make_c_reporter() {
    return PrkReporter{nullptr, &ConsoleReporter::write_test_result_line, &ConsoleReporter::write_run_summary_line,
                       this};
}

void ConsoleReporter::write_test_result_line(void* reporter_state, const PrkTestResult* result) {
    auto* const self = static_cast<ConsoleReporter*>(reporter_state);
    self->output_ << format_test_result_line(record_test_result(*result)) << '\n';
}

void ConsoleReporter::write_run_summary_line(void* reporter_state, const PrkRunSummary* summary) {
    auto* const self = static_cast<ConsoleReporter*>(reporter_state);
    self->output_ << format_run_summary_line(*summary) << '\n';
}

PrkReporter JsonReporter::make_c_reporter() {
    return PrkReporter{nullptr, &JsonReporter::collect_test_result, &JsonReporter::write_json_document, this};
}

void JsonReporter::collect_test_result(void* reporter_state, const PrkTestResult* result) {
    static_cast<JsonReporter*>(reporter_state)->results_.push_back(record_test_result(*result));
}

void JsonReporter::write_json_document(void* reporter_state, const PrkRunSummary* summary) {
    auto* const self = static_cast<JsonReporter*>(reporter_state);
    self->output_ << format_json_report(self->results_, *summary);
}

}  // namespace prk
