/**
 * @file json_reader.hpp
 * @brief A small strict JSON reader (#75) for the GitHub release document: objects, arrays, strings (with
 *        \uXXXX escapes and surrogate pairs), numbers, true, false, and null. Pure; no IO.
 */
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prk {

/** @brief One JSON value. Numbers keep their source text. */
struct JsonValue {
    /** @brief Kind of value. */
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    Kind kind = Kind::Null;                    ///< Which of the fields below is meaningful.
    bool boolean = false;                      ///< Kind::Boolean.
    std::string text;                          ///< Kind::String (decoded UTF-8) or Kind::Number (source text).
    std::vector<JsonValue> items;              ///< Kind::Array.
    std::map<std::string, JsonValue> members;  ///< Kind::Object (a later duplicate key wins).

    /**
     * @brief Returns the member @p name when this is an object that has it.
     * @param name Member name. @return The member, or nullptr. Pure function.
     */
    [[nodiscard]] const JsonValue* find_member(std::string_view name) const;

    /**
     * @brief Returns the string member @p name, or "" when it is missing or not a string. Pure function.
     * @param name Member name. @return The decoded string.
     */
    [[nodiscard]] std::string read_string_member(std::string_view name) const;
};

/**
 * @brief Parses one JSON document (surrounding white space allowed). Pure function.
 * @param text Document text. @param out_error Receives a short reason on failure.
 * @return The value, or std::nullopt when @p text is not valid JSON or nests deeper than 64 levels.
 */
[[nodiscard]] std::optional<JsonValue> parse_json_document(std::string_view text, std::string& out_error);

}  // namespace prk
