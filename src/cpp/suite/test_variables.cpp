/**
 * @file test_variables.cpp
 * @brief Implementation of `${name}` expansion and resolution for `prk test` (#82).
 */
#include "test_variables.hpp"

#include <vector>

namespace prk {

namespace {

/** @brief Tries to resolve @p variable; returns true and sets @p out_value when it resolves. */
bool resolve_one_variable(const TestVariable& variable, const TestVariables& known,
                          const PathExistsFunction& path_exists, std::string& out_value) {
    std::string missing;
    for (const std::string& candidate : variable.candidates) {
        std::optional<std::string> value = expand_test_variables(candidate, known, missing);
        if (!value) {
            continue;
        }
        if (!variable.is_path_list || (path_exists && path_exists(*value))) {
            out_value = std::move(*value);
            return true;
        }
    }
    return false;
}

/** @brief Tells whether a candidate of @p variable refers to another variable that is still pending. */
bool is_waiting_for_unresolved(const TestVariable& variable, const TestVariables& known,
                               const std::vector<const TestVariable*>& pending) {
    for (const std::string& candidate : variable.candidates) {
        std::string missing;
        if (!expand_test_variables(candidate, known, missing)) {
            for (const TestVariable* other : pending) {
                if (other->name == missing && other != &variable) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

std::optional<std::string> expand_test_variables(std::string_view text, const TestVariables& variables,
                                                 std::string& out_missing) {
    std::string expanded;
    expanded.reserve(text.size());
    std::size_t position = 0;
    while (position < text.size()) {
        if (text.compare(position, 3, "$${") == 0) {
            expanded += "${";
            position += 3;
            continue;
        }
        if (text.compare(position, 2, "${") != 0) {
            expanded += text[position++];
            continue;
        }
        const std::size_t close = text.find('}', position + 2);
        if (close == std::string_view::npos) {
            out_missing = std::string(text.substr(position));
            return std::nullopt;
        }
        const std::string_view name = text.substr(position + 2, close - position - 2);
        const auto found = variables.find(name);
        if (found == variables.end()) {
            out_missing = std::string(name);
            return std::nullopt;
        }
        expanded += found->second;
        position = close + 1;
    }
    return expanded;
}

TestVariables resolve_test_variables(const TestConfig& config, const TestVariables& builtins,
                                     const TestVariables& overrides, const PathExistsFunction& path_exists) {
    TestVariables known = builtins;
    for (const auto& [name, value] : overrides) {
        known[name] = value;
    }
    std::vector<const TestVariable*> pending;
    for (const TestVariable& variable : config.variables) {
        if (overrides.find(variable.name) == overrides.end()) {
            pending.push_back(&variable);
        }
    }
    bool is_progressing = true;
    while (is_progressing && !pending.empty()) {
        is_progressing = false;
        for (auto iterator = pending.begin(); iterator != pending.end();) {
            if (is_waiting_for_unresolved(**iterator, known, pending)) {
                ++iterator;
                continue;
            }
            std::string value;
            if (resolve_one_variable(**iterator, known, path_exists, value)) {
                known[(*iterator)->name] = std::move(value);
            }
            iterator = pending.erase(iterator);
            is_progressing = true;
        }
    }
    return known;
}

}  // namespace prk
