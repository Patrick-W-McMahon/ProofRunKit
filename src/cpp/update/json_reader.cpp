/**
 * @file json_reader.cpp
 * @brief Implementation of the small strict JSON reader (#75).
 */
#include "json_reader.hpp"

#include <cstdint>
#include <utility>

namespace prk {

namespace {

constexpr int kMaximumDepth = 64;

/** @brief Cursor over the document; every reader advances it and reports failure as false. */
class JsonCursor {
public:
    explicit JsonCursor(std::string_view text) : text_(text) {}

    /** @brief Reads one value at @p depth into @p out_value. */
    bool read_value(int depth, JsonValue& out_value) {
        skip_space();
        if (depth > kMaximumDepth || at_end()) {
            return fail(depth > kMaximumDepth ? "nested too deeply" : "unexpected end");
        }
        const char next = text_[position_];
        if (next == '{') {
            return read_object(depth, out_value);
        }
        if (next == '[') {
            return read_array(depth, out_value);
        }
        if (next == '"') {
            out_value.kind = JsonValue::Kind::String;
            return read_string(out_value.text);
        }
        if (next == '-' || (next >= '0' && next <= '9')) {
            return read_number(out_value);
        }
        return read_literal(out_value);
    }

    /** @brief True when only white space is left. */
    bool is_finished() {
        skip_space();
        return at_end();
    }

    [[nodiscard]] const std::string& error() const { return error_; }

private:
    bool at_end() const { return position_ >= text_.size(); }

    bool fail(const char* reason) {
        if (error_.empty()) {
            error_ = std::string(reason) + " at offset " + std::to_string(position_);
        }
        return false;
    }

    void skip_space() {
        while (!at_end() && (text_[position_] == ' ' || text_[position_] == '\t' || text_[position_] == '\n' ||
                             text_[position_] == '\r')) {
            ++position_;
        }
    }

    bool consume(char expected) {
        skip_space();
        if (at_end() || text_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    bool read_object(int depth, JsonValue& out_value) {
        out_value.kind = JsonValue::Kind::Object;
        ++position_;
        if (consume('}')) {
            return true;
        }
        do {
            skip_space();
            std::string name;
            if (at_end() || text_[position_] != '"' || !read_string(name)) {
                return fail("expected a member name");
            }
            if (!consume(':')) {
                return fail("expected ':'");
            }
            JsonValue member;
            if (!read_value(depth + 1, member)) {
                return false;
            }
            out_value.members[std::move(name)] = std::move(member);
        } while (consume(','));
        return consume('}') || fail("expected ',' or '}'");
    }

    bool read_array(int depth, JsonValue& out_value) {
        out_value.kind = JsonValue::Kind::Array;
        ++position_;
        if (consume(']')) {
            return true;
        }
        do {
            JsonValue item;
            if (!read_value(depth + 1, item)) {
                return false;
            }
            out_value.items.push_back(std::move(item));
        } while (consume(','));
        return consume(']') || fail("expected ',' or ']'");
    }

    bool read_hex_unit(std::uint32_t& out_unit) {
        if (position_ + 4 > text_.size()) {
            return false;
        }
        out_unit = 0;
        for (int index = 0; index < 4; ++index) {
            const char digit = text_[position_++];
            const int value = digit >= '0' && digit <= '9'   ? digit - '0'
                              : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
                              : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10
                                                             : -1;
            if (value < 0) {
                return false;
            }
            out_unit = (out_unit << 4) | static_cast<std::uint32_t>(value);
        }
        return true;
    }

    static void append_utf8(std::uint32_t code_point, std::string& out_text) {
        if (code_point < 0x80) {
            out_text += static_cast<char>(code_point);
        } else if (code_point < 0x800) {
            out_text += static_cast<char>(0xC0 | (code_point >> 6));
            out_text += static_cast<char>(0x80 | (code_point & 0x3F));
        } else if (code_point < 0x10000) {
            out_text += static_cast<char>(0xE0 | (code_point >> 12));
            out_text += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
            out_text += static_cast<char>(0x80 | (code_point & 0x3F));
        } else {
            out_text += static_cast<char>(0xF0 | (code_point >> 18));
            out_text += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
            out_text += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
            out_text += static_cast<char>(0x80 | (code_point & 0x3F));
        }
    }

    bool read_unicode_escape(std::string& out_text) {
        std::uint32_t unit = 0;
        if (!read_hex_unit(unit)) {
            return fail("bad \\u escape");
        }
        if (unit >= 0xD800 && unit <= 0xDBFF && text_.substr(position_, 2) == "\\u") {
            position_ += 2;
            std::uint32_t low = 0;
            if (!read_hex_unit(low) || low < 0xDC00 || low > 0xDFFF) {
                return fail("bad surrogate pair");
            }
            unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
        } else if (unit >= 0xD800 && unit <= 0xDFFF) {
            unit = 0xFFFD;
        }
        append_utf8(unit, out_text);
        return true;
    }

    bool read_string(std::string& out_text) {
        ++position_;
        while (!at_end()) {
            const char character = text_[position_++];
            if (character == '"') {
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20) {
                return fail("control character in string");
            }
            if (character != '\\') {
                out_text += character;
                continue;
            }
            if (at_end()) {
                break;
            }
            const char escape = text_[position_++];
            static constexpr std::string_view kEscapes = "\"\\/bfnrt";
            static constexpr std::string_view kMeanings = "\"\\/\b\f\n\r\t";
            const std::size_t found = kEscapes.find(escape);
            if (escape == 'u') {
                if (!read_unicode_escape(out_text)) {
                    return false;
                }
            } else if (found != std::string_view::npos) {
                out_text += kMeanings[found];
            } else {
                return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool read_number(JsonValue& out_value) {
        const std::size_t start = position_;
        if (text_[position_] == '-') {
            ++position_;
        }
        const std::size_t digits_start = position_;
        while (!at_end() && ((text_[position_] >= '0' && text_[position_] <= '9') || text_[position_] == '.' ||
                             text_[position_] == 'e' || text_[position_] == 'E' || text_[position_] == '+' ||
                             text_[position_] == '-')) {
            ++position_;
        }
        if (position_ == digits_start) {
            return fail("bad number");
        }
        out_value.kind = JsonValue::Kind::Number;
        out_value.text = std::string(text_.substr(start, position_ - start));
        return true;
    }

    bool read_literal(JsonValue& out_value) {
        const std::string_view rest = text_.substr(position_);
        if (rest.starts_with("true") || rest.starts_with("false")) {
            out_value.kind = JsonValue::Kind::Boolean;
            out_value.boolean = rest.starts_with("true");
            position_ += out_value.boolean ? 4 : 5;
            return true;
        }
        if (rest.starts_with("null")) {
            out_value.kind = JsonValue::Kind::Null;
            position_ += 4;
            return true;
        }
        return fail("unexpected character");
    }

    std::string_view text_;
    std::size_t position_ = 0;
    std::string error_;
};

}  // namespace

const JsonValue* JsonValue::find_member(std::string_view name) const {
    if (kind != Kind::Object) {
        return nullptr;
    }
    const auto found = members.find(std::string(name));
    return found != members.end() ? &found->second : nullptr;
}

std::string JsonValue::read_string_member(std::string_view name) const {
    const JsonValue* const member = find_member(name);
    return member != nullptr && member->kind == Kind::String ? member->text : std::string();
}

std::optional<JsonValue> parse_json_document(std::string_view text, std::string& out_error) {
    JsonCursor cursor(text);
    JsonValue value;
    if (!cursor.read_value(0, value)) {
        out_error = cursor.error();
        return std::nullopt;
    }
    if (!cursor.is_finished()) {
        out_error = "trailing text after the document";
        return std::nullopt;
    }
    return value;
}

}  // namespace prk
