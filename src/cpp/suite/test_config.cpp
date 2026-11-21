/**
 * @file test_config.cpp
 * @brief Implementation of the pure `prk.tests.json` parser (#82). Unknown keys are errors, so typos surface.
 */
#include "test_config.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "cpp/update/json_reader.hpp"

namespace prk {

namespace {

/** @brief Parsing state: the first error wins. */
struct ConfigReader {
    std::string error;  ///< "where: what", or empty.

    /** @brief Records @p what at @p where unless an error is already recorded; returns false. */
    bool fail(const std::string& where, const std::string& what) {
        if (error.empty()) {
            error = where + ": " + what;
        }
        return false;
    }
};

/** @brief Checks that @p object has only @p allowed keys. */
bool check_known_keys(const JsonValue& object, const std::set<std::string_view>& allowed, const std::string& where,
                      ConfigReader& reader) {
    for (const auto& [key, value] : object.members) {
        if (allowed.count(key) == 0) {
            return reader.fail(where, "unknown key '" + key + "'");
        }
    }
    return true;
}

/** @brief Reads a string or a list of strings into @p out_items. */
bool read_string_list(const JsonValue& value, const std::string& where, ConfigReader& reader,
                      std::vector<std::string>& out_items) {
    if (value.kind == JsonValue::Kind::String) {
        out_items.push_back(value.text);
        return true;
    }
    if (value.kind != JsonValue::Kind::Array) {
        return reader.fail(where, "expected a string or a list of strings");
    }
    for (const JsonValue& item : value.items) {
        if (item.kind != JsonValue::Kind::String) {
            return reader.fail(where, "expected only strings in the list");
        }
        out_items.push_back(item.text);
    }
    return true;
}

/** @brief Reads a whole number from 0 to @p maximum. */
bool read_whole_number(const JsonValue& value, std::uint64_t maximum, const std::string& where, ConfigReader& reader,
                       std::uint64_t& out_number) {
    const bool is_digits = value.kind == JsonValue::Kind::Number && !value.text.empty() && value.text.size() <= 12 &&
                           std::all_of(value.text.begin(), value.text.end(),
                                       [](unsigned char character) { return std::isdigit(character) != 0; });
    if (!is_digits || std::stoull(value.text) > maximum) {
        return reader.fail(where, "expected a whole number from 0 to " + std::to_string(maximum));
    }
    out_number = std::stoull(value.text);
    return true;
}

/** @brief Reads an object of string values into @p out_settings. */
bool read_environment(const JsonValue& value, const std::string& where, ConfigReader& reader,
                      std::vector<EnvironmentSetting>& out_settings) {
    if (value.kind != JsonValue::Kind::Object) {
        return reader.fail(where, "expected an object of strings");
    }
    for (const auto& [name, setting] : value.members) {
        if (name.empty() || name.find('=') != std::string::npos || setting.kind != JsonValue::Kind::String) {
            return reader.fail(where + "." + name, "expected a string value and a name without '='");
        }
        out_settings.emplace_back(name, setting.text);
    }
    return true;
}

/** @brief Reads `expect_exit_code`, `timeout_ms`, `template`, and `report` of one entry. */
bool read_entry_numbers_and_flags(const JsonValue& object, const std::string& where, ConfigReader& reader,
                                  TestEntry& entry) {
    std::uint64_t number = 0;
    if (const JsonValue* code = object.find_member("expect_exit_code")) {
        if (!read_whole_number(*code, 255, where + ".expect_exit_code", reader, number)) {
            return false;
        }
        entry.expected_exit_code = static_cast<int>(number);
    }
    if (const JsonValue* timeout = object.find_member("timeout_ms")) {
        if (!read_whole_number(*timeout, 86400000, where + ".timeout_ms", reader, entry.timeout_ms)) {
            return false;
        }
    }
    if (const JsonValue* is_template = object.find_member("template")) {
        if (is_template->kind != JsonValue::Kind::Boolean) {
            return reader.fail(where + ".template", "expected true or false");
        }
        entry.is_template = is_template->boolean;
    }
    if (const JsonValue* report = object.find_member("report")) {
        if (report->kind != JsonValue::Kind::String || (report->text != "json" && report->text != "none")) {
            return reader.fail(where + ".report", "expected \"json\" or \"none\"");
        }
        entry.is_report_read = report->text == "json";
    }
    return true;
}

/** @brief Reads one entry object of `setup` or `tests`. */
bool read_entry(const JsonValue& object, const std::string& where, ConfigReader& reader, TestEntry& entry) {
    static const std::set<std::string_view> kKeys = {
        "name",       "run",      "command", "args",          "cwd",           "env",         "expect_exit_code",
        "timeout_ms", "template", "report",  "expect_output", "reject_output", "skip_unless", "description"};
    if (object.kind != JsonValue::Kind::Object) {
        return reader.fail(where, "expected an object");
    }
    if (!check_known_keys(object, kKeys, where, reader)) {
        return false;
    }
    entry.name = object.read_string_member("name");
    if (!is_valid_test_entry_name(entry.name)) {
        return reader.fail(where + ".name", "expected a name of letters, digits, '_', '-', or '.'");
    }
    const std::string at = where + " (" + entry.name + ")";
    const JsonValue* run = object.find_member("run");
    const JsonValue* command = object.find_member("command");
    if ((run == nullptr) == (command == nullptr)) {
        return reader.fail(at, "needs exactly one of 'run' or 'command'");
    }
    const bool are_lists_read =
        (run == nullptr || read_string_list(*run, at + ".run", reader, entry.scripts)) &&
        (command == nullptr || read_string_list(*command, at + ".command", reader, entry.command));
    if (!are_lists_read || (entry.scripts.empty() && entry.command.empty())) {
        return reader.fail(at, "'run' or 'command' must not be empty");
    }
    const JsonValue* arguments = object.find_member("args");
    const JsonValue* skip_unless = object.find_member("skip_unless");
    const JsonValue* env = object.find_member("env");
    if ((arguments != nullptr && !read_string_list(*arguments, at + ".args", reader, entry.arguments)) ||
        (skip_unless != nullptr && !read_string_list(*skip_unless, at + ".skip_unless", reader, entry.skip_unless)) ||
        (env != nullptr && !read_environment(*env, at + ".env", reader, entry.env))) {
        return false;
    }
    entry.working_directory = object.read_string_member("cwd");
    entry.expected_output = object.read_string_member("expect_output");
    entry.rejected_output = object.read_string_member("reject_output");
    return read_entry_numbers_and_flags(object, at, reader, entry);
}

/** @brief Reads the list @p key (`setup` or `tests`) into @p out_entries; names must be unique overall. */
bool read_entries(const JsonValue& root, std::string_view key, ConfigReader& reader, std::set<std::string>& names,
                  std::vector<TestEntry>& out_entries) {
    const JsonValue* list = root.find_member(key);
    if (list == nullptr) {
        return true;
    }
    if (list->kind != JsonValue::Kind::Array) {
        return reader.fail(std::string(key), "expected a list of entries");
    }
    for (std::size_t index = 0; index < list->items.size(); ++index) {
        TestEntry entry;
        const std::string where = std::string(key) + "[" + std::to_string(index) + "]";
        if (!read_entry(list->items[index], where, reader, entry)) {
            return false;
        }
        if (!names.insert(entry.name).second) {
            return reader.fail(where, "duplicate entry name '" + entry.name + "'");
        }
        out_entries.push_back(std::move(entry));
    }
    return true;
}

/** @brief Reads `variables`. */
bool read_variables(const JsonValue& root, ConfigReader& reader, std::vector<TestVariable>& out_variables) {
    const JsonValue* variables = root.find_member("variables");
    if (variables == nullptr) {
        return true;
    }
    if (variables->kind != JsonValue::Kind::Object) {
        return reader.fail("variables", "expected an object");
    }
    for (const auto& [name, value] : variables->members) {
        TestVariable variable{name, {}, value.kind == JsonValue::Kind::Array};
        if (!is_valid_test_entry_name(name)) {
            return reader.fail("variables." + name, "invalid variable name");
        }
        if (!read_string_list(value, "variables." + name, reader, variable.candidates)) {
            return false;
        }
        out_variables.push_back(std::move(variable));
    }
    return true;
}

/** @brief Reads `defaults`. */
bool read_defaults(const JsonValue& root, ConfigReader& reader, TestConfig& config) {
    const JsonValue* defaults = root.find_member("defaults");
    if (defaults == nullptr) {
        return true;
    }
    if (defaults->kind != JsonValue::Kind::Object ||
        !check_known_keys(*defaults, {"env", "timeout_ms"}, "defaults", reader)) {
        return reader.fail("defaults", "expected an object with env and timeout_ms");
    }
    const JsonValue* env = defaults->find_member("env");
    const JsonValue* timeout = defaults->find_member("timeout_ms");
    return (env == nullptr || read_environment(*env, "defaults.env", reader, config.default_env)) &&
           (timeout == nullptr ||
            read_whole_number(*timeout, 86400000, "defaults.timeout_ms", reader, config.default_timeout_ms));
}

}  // namespace

bool is_valid_test_entry_name(std::string_view name) {
    return !name.empty() && name.size() <= 100 && std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-' || character == '.';
    });
}

std::optional<TestConfig> parse_test_config(std::string_view text, std::string& out_error) {
    std::string json_error;
    const std::optional<JsonValue> root = parse_json_document(text, json_error);
    ConfigReader reader;
    TestConfig config;
    std::set<std::string> names;
    static const std::set<std::string_view> kKeys = {"schema_version", "description", "variables", "defaults",
                                                     "runner",         "work_dir",    "setup",     "tests"};
    if (!root) {
        reader.fail("prk.tests.json", "not valid JSON (" + json_error + ")");
    } else if (root->kind != JsonValue::Kind::Object) {
        reader.fail("prk.tests.json", "expected an object");
    } else if (const JsonValue* version = root->find_member("schema_version");
               version == nullptr || version->kind != JsonValue::Kind::Number || version->text != "1") {
        reader.fail("schema_version", "expected 1");
    } else if (check_known_keys(*root, kKeys, "prk.tests.json", reader) &&
               read_variables(*root, reader, config.variables) && read_defaults(*root, reader, config) &&
               read_entries(*root, "setup", reader, names, config.setup) &&
               read_entries(*root, "tests", reader, names, config.tests)) {
        if (const JsonValue* runner = root->find_member("runner"); runner != nullptr) {
            if (runner->kind != JsonValue::Kind::String || runner->text.empty()) {
                reader.fail("runner", "expected a non-empty string");
            }
            config.runner = runner->text;
        }
        config.work_dir = root->read_string_member("work_dir");
    }
    out_error = reader.error;
    if (!out_error.empty()) {
        return std::nullopt;
    }
    return config;
}

}  // namespace prk
